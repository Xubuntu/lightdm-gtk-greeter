#!/usr/bin/env python3
"""Exercise the real GTK binary against a minimal LightDM wire-protocol fixture.

No passwords, AD connections, PAM modules or privileged LightDM daemon are used.
Run inside Xvfb; xdotool supplies actual keyboard events. Optional PNG captures
use GDK directly, so they show the running application, not a reconstructed UI.
"""
import configparser
from contextlib import closing
import ctypes
import os
from pathlib import Path
import select
import socket
import struct
import subprocess
import tempfile
import time
import unittest

BINARY = os.environ.get('GREETER_TEST_BINARY', './src/lightdm-gtk-greeter')
CAPTURE = os.environ.get('GREETER_TEST_CAPTURE_DIR')


def integer(value):
    return struct.pack('!I', value)


def string(value):
    data = value.encode('utf-8')
    return integer(len(data)) + data


def take_string(data, offset=0):
    length = struct.unpack_from('!I', data, offset)[0]
    end = offset + 4 + length
    return data[offset + 4:end].decode('utf-8'), end


def capture(name, expected_color=None, present=True):
    if not CAPTURE and expected_color is None:
        return
    time.sleep(0.2)
    gdk = ctypes.CDLL('libgdk-3.so.0')
    pix = ctypes.CDLL('libgdk_pixbuf-2.0.so.0')
    obj = ctypes.CDLL('libgobject-2.0.so.0')
    gdk.gdk_init.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    gdk.gdk_init(None, None)
    gdk.gdk_get_default_root_window.restype = ctypes.c_void_p
    root = gdk.gdk_get_default_root_window()
    gdk.gdk_window_get_width.argtypes = [ctypes.c_void_p]
    gdk.gdk_window_get_height.argtypes = [ctypes.c_void_p]
    gdk.gdk_pixbuf_get_from_window.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    gdk.gdk_pixbuf_get_from_window.restype = ctypes.c_void_p
    image = gdk.gdk_pixbuf_get_from_window(root, 0, 0,
                gdk.gdk_window_get_width(root), gdk.gdk_window_get_height(root))
    pix.gdk_pixbuf_savev.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
                                   ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    try:
        if expected_color:
            for func in ('gdk_pixbuf_get_width', 'gdk_pixbuf_get_height',
                         'gdk_pixbuf_get_rowstride', 'gdk_pixbuf_get_n_channels'):
                getattr(pix, func).argtypes = [ctypes.c_void_p]
            pix.gdk_pixbuf_get_pixels.argtypes = [ctypes.c_void_p]
            pix.gdk_pixbuf_get_pixels.restype = ctypes.c_void_p
            width, height = pix.gdk_pixbuf_get_width(image), pix.gdk_pixbuf_get_height(image)
            stride, channels = pix.gdk_pixbuf_get_rowstride(image), pix.gdk_pixbuf_get_n_channels(image)
            data = ctypes.string_at(pix.gdk_pixbuf_get_pixels(image), stride * height)
            matches = sum(all(abs(data[y * stride + x * channels + c] - expected_color[c]) <= 1
                              for c in range(3)) for y in range(height) for x in range(width))
            assert (matches > 10) == present, f'Unexpected rendered color {expected_color}: {matches} pixels'
        if CAPTURE:
            Path(CAPTURE).mkdir(parents=True, exist_ok=True)
            assert pix.gdk_pixbuf_savev(image, os.fsencode(Path(CAPTURE) / (name + '.png')),
                                       b'png', None, None, None)
    finally:
        obj.g_object_unref.argtypes = [ctypes.c_void_p]
        obj.g_object_unref(image)



class Greeter:
    def __init__(self, suppress=False, hidden=False, remember=True, last='Rafael@example.test', css=None):
        self.temp = tempfile.TemporaryDirectory(prefix='greeter-test-')
        self.root = Path(self.temp.name)
        conf = self.root / 'conf/lightdm'
        conf.mkdir(parents=True)
        self.state = self.root / 'cache/lightdm-gtk-greeter/state'
        self.state.parent.mkdir(parents=True)
        self.state.write_text('[greeter]\nlast-user=' + last + '\n')
        settings = ('[greeter]\nbackground=#243447\nuser-background=false\n'
                    'indicators=~session;~a11y;~power\n'
                    'at-spi-enabled=false\nclock-format=12:16\ndate-format=Tuesday, December 5\n'
                    'suppress-session-warnings=' + str(suppress).lower() + '\n'
                    'remember-last-user=' + str(remember).lower() + '\n')
        if css:
            settings += 'css-file=' + str(css) + '\n'
        (conf / 'lightdm-gtk-greeter.conf').write_text(settings)
        self.server, client = socket.socketpair()
        self.server.settimeout(5)
        env = dict(os.environ, XDG_CONFIG_DIRS=str(self.root / 'conf'),
                   XDG_CACHE_HOME=str(self.root / 'cache'), XDG_SEAT='seat0',
                   LIGHTDM_TO_SERVER_FD=str(client.fileno()),
                   LIGHTDM_FROM_SERVER_FD=str(client.fileno()), NO_AT_BRIDGE='1')
        self.log = open(self.root / 'greeter.log', 'w+')
        self.proc = subprocess.Popen([BINARY], env=env, pass_fds=(client.fileno(),),
                                     stdout=self.log, stderr=self.log)
        client.close()
        try:
            kind, _ = self.recv()
            assert kind == 0, kind  # CONNECT
            hints = {'hide-users': 'true' if hidden else 'false', 'has-guest-account': 'false'}
            self.send(0, string('1.32.0') + b''.join(string(k) + string(v) for k, v in hints.items()))
            self.seq, self.user = self.auth()
            # Builder, background and focus must be ready before input is sent.
            time.sleep(0.2)
        except BaseException:
            self.close()
            raise

    def send(self, kind, payload):
        self.server.sendall(integer(kind) + integer(len(payload)) + payload)

    def exact(self, size):
        data = b''
        while len(data) < size:
            part = self.server.recv(size - len(data))
            if not part:
                self.log.flush()
                raise AssertionError('Greeter disconnected: ' + (self.root / 'greeter.log').read_text())
            data += part
        return data

    def recv(self):
        header = self.exact(8)
        kind, size = struct.unpack('!II', header)
        return kind, self.exact(size)

    def auth(self):
        while True:
            kind, payload = self.recv()
            if kind == 6:  # optional SET_LANGUAGE
                continue
            assert kind == 1, kind
            return struct.unpack_from('!I', payload)[0], take_string(payload, 4)[0]

    def prompt(self, *messages):
        self.send(1, integer(self.seq) + string(self.user) + integer(len(messages)) +
                  b''.join(integer(style) + string(text) for style, text in messages))
        time.sleep(0.2)

    def end(self, success=True):
        self.send(2, integer(self.seq) + string(self.user) + integer(0 if success else 7))

    def quiet(self):
        assert not select.select([self.server], [], [], 0.3)[0], 'Unexpected protocol response'
        assert self.proc.poll() is None, 'Greeter exited'

    def type(self, text):
        subprocess.run(['xdotool', 'type', '--clearmodifiers', '--delay', '1', text], check=True)

    def key(self, key):
        subprocess.run(['xdotool', 'key', '--clearmodifiers', key], check=True, timeout=3)
        time.sleep(0.15)

    def response(self):
        kind, data = self.recv()
        assert kind == 3, kind
        assert struct.unpack_from('!I', data)[0] == 1
        return take_string(data, 4)[0]

    def cached(self):
        parser = configparser.ConfigParser()
        parser.read(self.state)
        return parser['greeter']['last-user']

    def session(self, success=True):
        kind, _ = self.recv()
        while kind == 6:
            kind, _ = self.recv()
        assert kind == 4, kind
        self.send(3, integer(0 if success else 1))
        time.sleep(0.15)

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.server.close()
        self.log.close()
        if CAPTURE:
            Path(CAPTURE).mkdir(parents=True, exist_ok=True)
            with open(Path(CAPTURE) / 'runtime.log', 'a') as dest:
                dest.write((self.root / 'greeter.log').read_text())
        self.temp.cleanup()


class Smoke(unittest.TestCase):
    def launch(self, **kwargs):
        app = Greeter(**kwargs)
        self.addCleanup(app.close)
        return app

    def test_cached_directory_user_and_failed_attempt(self):
        app = self.launch()
        self.assertEqual(app.user, 'Rafael@example.test')
        app.prompt((1, 'Password:'))
        capture('last-user')
        app.type('not-a-real-password')
        app.key('Return')
        self.assertEqual(app.response(), 'not-a-real-password')
        app.end(False)
        app.seq, app.user = app.auth()
        self.assertEqual(app.cached(), 'Rafael@example.test')
        app.prompt((1, 'Password:'))
        capture('authentication-error', (255, 180, 171))

    def test_other_user_preserves_upn_and_success_cache(self):
        app = self.launch(suppress=True)
        app.prompt((1, 'Password:'))
        app.key('alt+o')
        app.seq, app.user = app.auth()
        self.assertEqual(app.user, '')
        app.prompt((2, 'Username:'))
        app.type('Another.User@EXAMPLE.test')
        app.key('Tab')
        app.seq, app.user = app.auth()
        self.assertEqual(app.user, 'Another.User@EXAMPLE.test')
        self.assertEqual(app.cached(), 'Rafael@example.test')
        app.prompt((1, 'Password:'))
        capture('other-user')
        app.type('test-password')
        app.key('Return')
        self.assertEqual(app.response(), 'test-password')
        app.end()
        app.session()
        self.assertEqual(app.cached(), 'Another.User@EXAMPLE.test')
        self.assertNotIn('test-password', app.state.read_text())

    def test_no_prompt_requires_click_by_default(self):
        app = self.launch()
        app.end()
        app.quiet()
        # start_session_sync can temporarily hold the X server while waiting
        # for this fixture's reply. Send the activation without blocking it.
        sender = subprocess.Popen(['xdotool', 'key', '--clearmodifiers', 'Return'])
        try:
            app.session()
            self.assertEqual(sender.wait(timeout=3), 0)
        finally:
            if sender.poll() is None:
                sender.kill()
                sender.wait()

    def test_opt_in_direct_session(self):
        app = self.launch(suppress=True)
        app.end()
        app.session()
        self.assertEqual(app.cached(), 'Rafael@example.test')

    def test_warning_flag_preserves_mfa_and_errors(self):
        app = self.launch(suppress=True)
        app.prompt((4, 'Warning: arcfour is deprecated'))
        capture('suppressed-info', (224, 229, 235), present=False)
        app.prompt((1, 'Verification code:'))
        app.quiet()  # Must never automatically answer a PAM prompt.
        app.type('123456')
        app.key('Return')
        self.assertEqual(app.response(), '123456')
        app.prompt((3, 'RC4 policy denied this login'), (2, 'Confirm your identity:'))
        app.quiet()
        capture('pam-error-and-question', (255, 180, 171))

    def test_hidden_users_do_not_restore_or_write_cache(self):
        app = self.launch(suppress=True, hidden=True)
        self.assertEqual(app.user, '')
        app.user = 'Private.User@example.test'
        app.end()
        app.session()
        self.assertEqual(app.cached(), 'Rafael@example.test')

    def test_remember_disabled_does_not_restore(self):
        app = self.launch(suppress=True, remember=False)
        self.assertNotEqual(app.user, 'Rafael@example.test')
        app.user = 'New.User@example.test'
        app.end()
        app.session()
        self.assertEqual(app.cached(), 'Rafael@example.test')

    def test_warning_visible_by_default(self):
        app = self.launch()
        app.prompt((4, 'Warning: arcfour is deprecated'))
        capture('visible-info', (224, 229, 235))

    def test_external_css_and_invalid_css_fallback(self):
        with tempfile.TemporaryDirectory(prefix='greeter-css-') as folder:
            path = Path(folder) / 'gtk.css'
            for css, color in [('#login_window entry:focus { border-bottom-color: #ff00ff; }',
                                (255, 0, 255)),
                               ('#login_window { invalid-property: ???; }', (96, 205, 255))]:
                with self.subTest(css=css):
                    path.write_text(css)
                    with closing(Greeter(css=path)) as app:
                        app.prompt((1, 'Password:'))
                        capture('external-css' if color[0] == 255 else 'fallback-css', color)

    def test_failed_session_does_not_replace_cache(self):
        app = self.launch(suppress=True)
        app.user = 'Different@example.test'
        app.end()
        app.session(False)
        app.seq, app.user = app.auth()
        self.assertEqual(app.cached(), 'Rafael@example.test')


if __name__ == '__main__':
    unittest.main()
