<h1 align="center">Welcome to the aurorachat repository!</h1>
This is the 3DS client for Aurorachat.<br>
For more clients and stuff, see the <a href="https://github.com/Unitendo/aurorachat">main repo</a>.
The license, code of conduct, and security/contributing guidelines in the main repo also apply here.

<br>This repository is <b>open</b> for contributions! If you'd like to, you may open a PR or an issue, contributing helps us as we develop aurorachat!

<h1 align="center">How to build aurorachat</h1>

Install devkitpro with the 3DS development libraries and make, then execute the following commands based on your OS:

Windows:
```sh
pacman -S 3ds-opusfile
git clone https://github.com/Unitendo/aurorachat-3ds
cd aurorachat
make
make cia
```

Arch Linux or other distros with pacman:
```sh
sudo pacman -S 3ds-opusfile
git clone https://github.com/Unitendo/aurorachat-3ds
cd aurorachat
make
make cia
```

Other Linux distros without pacman:
```sh
sudo dkp-pacman -S 3ds-opusfile
git clone https://github.com/Unitendo/aurorachat-3ds
cd aurorachat
make
make cia
```

(At least that's what I think you gotta do)
