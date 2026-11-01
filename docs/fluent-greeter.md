# Greeter Fluent para ambientes corporativos

## Mapa e plano de refatoração

Esta árvore é o **lightdm-gtk-greeter 2.0.9**, em C e GTK 3/Autotools.
Não contém a implementação Vala do slick-greeter nem um frontend Python.
Python é usado apenas na automação de teste.

| Componente | Ponto de extensão | Refatoração implementada |
| --- | --- | --- |
| Painel de login | `src/lightdm-gtk-greeter.glade`, `main()` | Formulário sem cartão, avatar de 112 px, identidade, credenciais e botão de seta; posição proporcional ao monitor ativo |
| Relógio | `clock_timeout_thread()` | Relógio superior independente dos indicadores, data localizada e atualização somente quando o texto muda |
| Troca de identidade | `load_user_list()`, `set_displayed_user()`, `other_user_clicked_cb()` | Seletor inferior esquerdo e ação explícita Outro usuário (Alt+O no idioma inglês); conta AD anterior permanece selecionável mesmo sem enumeração |
| Autenticação | `start_authentication()`, `process_prompts()`, `authentication_complete_cb()` | Mantém o protocolo LightDM/PAM, limpa senha ao alternar identidade, mostra desafios e erros inline |
| Persistência | `start_session()`, `greeterconfiguration.c` | Grava a identidade apenas após LightDM aceitar o início da sessão, nunca ao selecionar usuário ou após tentativa malsucedida |
| Sessão e energia | `set_session()`, indicadores existentes | Reutiliza sessões instaladas e permissões LightDM; controles no canto inferior direito, F9–F12 e confirmação de desligamento preservados |
| Avatar | `greeteruserimage.c` | Foto fornecida pelo LightDM/AccountsService, recorte circular centralizado e fallback neutro desenhado com Cairo |
| Tema | `data/gtk.css`, provedores CSS de `main()` | Paleta, estados, margens e cantos externos; fallback compilado idêntico quando o arquivo não existe; alto contraste remove os provedores personalizados |

A composição segue as referências fornecidas: relógio grande na parte superior,
formulário abaixo do centro, troca de usuário à esquerda e ações à direita.
A referência visual orienta espaçamento e hierarquia; não incorpora os textos,
marcas d'água, clima ou outros elementos acessórios das imagens. Não há blur,
requisição de rede para imagens, compositor obrigatório ou animação de entrada.
O único movimento novo é a revelação do erro (120 ms), respeitando a preferência
GTK de desativar animações. Em telas compactas com texto ampliado, o relógio
é reduzido e o avatar decorativo é ocultado para preservar os campos e a navegação. Segoe UI depende de instalação/licença do sistema;
Noto Sans e DejaVu Sans são alternativas. Não se exige uma fonte proprietária.

## Configuração

A instalação distribui `gtk.css` em `${datadir}/lightdm-gtk-greeter/gtk.css`.
O mesmo CSS é compilado no binário para permitir execução na árvore de build.
Para customizar sem recompilar:

```ini
# /etc/lightdm/lightdm-gtk-greeter.conf
[greeter]
css-file=/etc/lightdm/gtk.css
background=/usr/share/backgrounds/empresa.jpg
user-background=false
remember-last-user=true
suppress-session-warnings=false
clock-format=%H:%M
date-format=%A, %e %B
position=50% 60%
panel-position=bottom
```

Copie `data/gtk.css` para `/etc/lightdm/gtk.css`, mantenha-o legível pelo usuário
do greeter e reinicie a sessão do greeter para aplicar alterações. CSS inválido
ou indisponível é registrado e o fallback continua funcionando. As cores
`greeter_fg`, `greeter_muted`, `greeter_accent`, `greeter_input`, `greeter_hover`
e `greeter_error` ficam no início do arquivo. Os campos e botões usam raio de
8 px; não há uma caixa externa com sombra. Arquivos de configuração em
`lightdm-gtk-greeter.conf.d` continuam funcionando.

O cache fica em `$XDG_CACHE_HOME/lightdm-gtk-greeter[/<seat>]/state`, no usuário
do greeter. É um dado de apresentação, nunca uma autorização de login. Guarda
somente a identidade e a sessão; nenhuma senha, token ou resposta PAM. A foto
é obtida novamente da API do LightDM, quando disponível; contas de domínio não
enumeradas usam o avatar padrão. `remember-last-user=false` impede leitura e
novas gravações de identidade. Não apaga um cache anterior; para remover dados
preexistentes, remova `last-user` desse arquivo com o greeter parado.
`greeter-hide-users=true` na configuração do LightDM prevalece: nenhuma conta
anterior é restaurada ou gravada enquanto essa política estiver ativa.

## Active Directory e confirmação de sessão

O nome digitado é enviado ao LightDM sem conversão de caixa, adição de domínio
ou alteração de `@` e `\\`. São aceitos UPN, sAMAccountName e `DOMINIO\\usuario`
quando o PAM/SSSD/winbind do host os aceita. O greeter não faz bind LDAP nem
configura a máquina no domínio. MFA, expiração e troca de senha continuam sendo
conversas PAM; a legenda acessível e o placeholder acompanham cada desafio.

`suppress-session-warnings=true` é opt-in e tem dois efeitos delimitados:

1. Após autenticação bem-sucedida sem perguntas, inicia a sessão diretamente,
   dispensando o clique adicional que este greeter normalmente exige.
2. Oculta mensagens **informativas** (`PAM_TEXT_INFO`) que mencionam RC4 ou
   Arcfour. O LightDM não fornece um código de aviso mais específico: essa parte
   do filtro usa texto, pode não reconhecer outros nomes/traduções e deve ser
   habilitada apenas após verificar as mensagens do backend de destino.

Erros (`PAM_ERROR_MSG`) e perguntas (`PAM_PROMPT_*`) nunca são filtrados ou
respondidos automaticamente. Avisos de troca de senha e MFA seguem visíveis.
Este fork não contém um diálogo de teste do Mint/Slick; um backend que exige uma
resposta de confirmação precisa ser identificado antes de criar um adaptador
específico. A flag não ativa RC4 nem corrige política Kerberos. Falhas de início
de sessão continuam aparecendo e não substituem a última identidade válida.

## Build e validação

Dependências principais: compilador C, Autotools, gettext/autopoint,
gobject-introspection, GTK 3, liblightdm-gobject, libX11, pkg-config e
xfce4-dev-tools. Ayatana/IDO são opcionais. No Debian o pacote de desenvolvimento
LightDM se chama `liblightdm-gobject-dev`; no Ubuntu, `liblightdm-gobject-1-dev`.

```sh
NOCONFIGURE=1 ./autogen.sh
mkdir -p build
cd build
../configure --with-libindicator=ayatana
make -j4
make check
make distcheck DISTCHECK_CONFIGURE_FLAGS=--with-libindicator=ayatana
```

A suíte usa o binário GTK real com socketpair e um servidor LightDM simulado,
Xvfb, D-Bus isolado e eventos de teclado enviados por xdotool. Requer Python 3,
`xvfb`, `xauth`, `xdotool` e `dbus-run-session`; o pacote `adwaita-icon-theme`
fornece os ícones padrão, com `librsvg2-common` para decodificar SVG. Na ausência de ferramentas, o runner sinaliza **SKIP**,
e isso não comprova o funcionamento do greeter. Nenhum teste acessa PAM/AD real
ou a configuração do sistema. `GREETER_TEST_CAPTURE_DIR=/tmp/captures make check`
salva capturas do processo GTK para inspeção.

A validação cobre cache de usuário AD não enumerado, erro e nova tentativa,
alternância para Outro usuário, preservação exata do UPN, início de sessão,
confirmação opt-in, desafios PAM/MFA, erros RC4, política de usuários ocultos e
falha de sessão. O teste de erro também verifica pixels da cor de erro, evitando
aceitar texto ilegível sobre o fundo.

A implantação ainda deve validar login/logout, avatar do diretório, MFA,
expiração de senha e múltiplos monitores com o LightDM e o domínio da empresa.
Os testes de protocolo não substituem essas verificações nem constituem medição
de consumo de memória ou latência no hardware final.
