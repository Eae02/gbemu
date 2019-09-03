A basic gameboy & gameboy color emulator. The emulator can run quite a few games including Tetris and Link's Awakening.

There are quite a few features that are missing / broken, such as CGB's HDMA, some audio stuff and less common memory bank controllers.

### Build Commands
```
./GenerateFont.sh <path to a ttf to embed>
mkdir .build
cd .build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

![Link's Awakening Screenshot](https://raw.githubusercontent.com/Eae02/gbemu/master/ImgZelda.png)
![Tetris Screenshot](https://raw.githubusercontent.com/Eae02/gbemu/master/ImgTetris.png)
