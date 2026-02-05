FROM ghcr.io/eclipse-score/devcontainer:latest

# Define a build argument for the username (default is 'invalidusername' if not provided)
ARG USERNAME=invalidusername

# RENAME 'vscode' -> ${USERNAME}
RUN usermod -l ${USERNAME} vscode \
    && groupmod -n ${USERNAME} vscode \
    && usermod -d /home/${USERNAME} -m ${USERNAME}

# GRANT SUDO (Using the variable)
RUN echo "${USERNAME} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/${USERNAME} \
    && chmod 0440 /etc/sudoers.d/${USERNAME}
