#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# vi: set ft=python :
"""
Launchpad Bug Tracker uses launchpadlib to get the bugs.

Based on https://github.com/ubuntu/yaru/blob/master/.github/lpbugtracker.py
"""

import os
import subprocess
import logging
from launchpadlib.launchpad import Launchpad

log = logging.getLogger("lpbugtracker")
log.setLevel(logging.DEBUG)

HUB = ".github/hub"
GH_OWNER = "Xubuntu"
GH_REPO = "lightdm-gtk-greeter"

LP_SOURCE_NAME = "lightdm-gtk-greeter"
LP_SOURCE_URL_NAME = "lightdm-gtk-greeter"

HOME = os.path.expanduser("~")
CACHEDIR = os.path.join(HOME, ".launchpadlib", "cache")


def main():
    lp_bugs = get_lp_bugs()
    if len(lp_bugs) == 0:
        return

    gh_bugs = get_gh_bugs()

    for id in lp_bugs:
        if id in gh_bugs.keys():
            if lp_bugs[id]["closed"] and gh_bugs[id]["status"] != "closed":
                close_issue(gh_bugs[id]["id"], lp_bugs[id]["status"])
        elif not lp_bugs[id]["closed"]:
            create_issue(id, lp_bugs[id]["title"], lp_bugs[id]["link"])


def get_lp_bugs():
    """Get a list of bugs from Launchpad"""

    lp = Launchpad.login_anonymously(
        "%s LP bug checker" % LP_SOURCE_NAME, "production", CACHEDIR, version="devel"
    )

    ubuntu = lp.distributions["ubuntu"]
    archive = ubuntu.main_archive

    packages = archive.getPublishedSources(source_name=LP_SOURCE_NAME)
    package = ubuntu.getSourcePackage(name=packages[0].source_package_name)

    bug_tasks = package.searchTasks(status=["New", "Opinion",
                                            "Invalid", "Won't Fix",
                                            "Expired", "Confirmed",
                                            "Triaged", "In Progress",
                                            "Fix Committed", "Fix Released",
                                            "Incomplete"])
    bugs = {}

    for task in bug_tasks:
        id = str(task.bug.id)
        title = task.title.split(": ")[1]
        status = task.status
        closed = status in ["Invalid", "Won't Fix", "Expired", "Fix Released"]
        link = "https://bugs.launchpad.net/ubuntu/+source/{}/+bug/{}".format(LP_SOURCE_URL_NAME, id)
        bugs[id] = {"title": title, "link": link, "status": status, "closed": closed}

    return bugs


def get_gh_bugs():
    """Get the list of the LP bug already tracked in GitHub.

    Launchpad bugs tracked on GitHub have a title like

    "LP#<id> <title>"

    this function returns a list of the "LP#<id>" substring for each bug,
    open or closed, found on the repository on GitHub.
    """

    output = subprocess.check_output(
        [HUB, "issue", "--labels", "Launchpad", "--state", "all", "--format", "%I %S %t%n"]
    )
    bugs = {}
    for line in output.decode().split("\n"):
        if "LP#" in line:
            id, status, lpid, title = line.strip().split(" ", 3)
            lpid = lpid[3:]
            bugs[lpid] = {"id": id, "status": status, "title": title}
    return bugs


def create_issue(id, title, weblink):
    """ Create a new Bug using HUB """
    print("creating:", id, title, weblink)
    #subprocess.run(
    print(
        [
            HUB,
            "issue",
            "create",
            "--message",
            "LP#{} {}".format(id, title),
            "--message",
            "Reported first on Launchpad at {}".format(weblink),
            "-l",
            "Launchpad",
        ]
    )


def close_issue(id, status):
    """ Close the Bug using HUB and leave a comment """
    print("closing:", id, status)
    #subprocess.run(
    print(
        [
            HUB,
            "api",
            "repos/{}/{}/issues/{}/comments".format(GH_OWNER, GH_REPO, id),
            "--field",
            "body=Issue closed on Launchpad with status: {}".format(status)
        ]
    )

    #subprocess.run(
    print(
        [
            HUB,
            "issue",
            "update",
            id,
            "--state",
            "closed"
        ]
    )


if __name__ == "__main__":
    main()
