bannertool.exe makebanner -i ../meta/banner.png -a ../meta/banner.wav -o banner.bnr
bannertool.exe makesmdh -s "aurorachat" -l "A real-time chatting app for the Nintendo 3DS" -p "The team at Unitendo" -i ../meta/icon.png  -o icon.icn
makerom.exe -f cia -o ../aurorachat-3ds.cia -DAPP_ENCRYPTED=false -rsf app.rsf -target t -exefslogo -elf ../aurorachat-3ds.elf -icon icon.icn -banner banner.bnr
echo Finished! CIA has been built!
