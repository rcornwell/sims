install_linux() {
    sudo apt-get update -myq
    sudo apt-get install -y libegl1-mesa-dev libgles2-mesa-dev
    sudo apt-get install -y libx11-dev libxt-dev libsdl2-dev
}

install_osx() {
    true
}

"$1"
