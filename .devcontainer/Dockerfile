FROM ghcr.io/eclipse-score/devcontainer:latest

# this  basically allows QNX license to be checked for the authorized user inside the container .
# it cannot be done at runtime with for ex "sudo sed -i 's/^vscode:/<ntusername>:/' /etc/passwd" because docker will complain about "vscode" user not existing at next startup
ARG USERNAME=invalidusername

# RENAME 'vscode' -> ${USERNAME}
RUN usermod -l ${USERNAME} vscode \
    && groupmod -n ${USERNAME} vscode \
    && usermod -d /home/${USERNAME} -m ${USERNAME}

# GRANT SUDO , is someone using sudo in devcontainer ?
RUN echo "${USERNAME} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/${USERNAME} \
    && chmod 0440 /etc/sudoers.d/${USERNAME}
