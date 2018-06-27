FROM ubuntu:18.04

# curl - For testing
# emacs - For Spacemacs
# clang-6.0 - Compiler. Binary /usr/bin/clang-6.0
# clang - Provides binary /usr/bin/clang
# build-essential - For few header files.
# git
# Setup Spacemacs

RUN apt-get update && apt-get install -y \
    curl \
    emacs25-nox \
    clang-6.0 \
    clang \
    build-essential \
    git \
    tmux \
    python3 \
    telnet \
    && git clone https://github.com/syl20bnr/spacemacs /root/.emacs.d/

COPY .spacemacs-default /root/.spacemacs
COPY .tmux.conf-default /root/.tmux.conf

RUN emacs -nw -batch -u root -q -kill

RUN echo 'TERM=xterm-256color' >> /root/.bashrc

CMD ["bash"]


