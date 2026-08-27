<div align="center">
  <img src="https://github.com/Unitendo/aurorachat-3ds/blob/main/meta/banner.png" align="center"></img>
  <p align="center">A chatting application for Homwbrewed Nintendo consoles</p>
</div>

<div align="center">
  Welcome to the official repository for the Nintendo 3DS client for aurorachat!
</div>

<h1 align="center">How to build aurorachat</h1>

Install devkitpro with the 3DS development libraries and make, then execute the following commands based on your OS:

Windows:
```sh
pacman -S 3ds-opusfile
git clone https://github.com/Unitendo/aurorachat-3ds
cd aurorachat-3ds
make
```

Arch Linux or other distros with pacman:
```sh
sudo pacman -S 3ds-opusfile
git clone https://github.com/Unitendo/aurorachat-3ds
cd aurorachat-3ds
make
```

Other Linux distros without pacman:
```sh
sudo dkp-pacman -S 3ds-opusfile
git clone https://github.com/Unitendo/aurorachat-3ds
cd aurorachat-3ds
make
```
