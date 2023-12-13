cd /home/OS/OS-p4
make clean
make
cd benchmark
make clean
make
cd ..
fusermount -u /tmp/OS/mountdir
rm -rf /home/OS/OS-p4/DISKFILE
/home/OS/OS-p4/rufs -s /tmp/OS/mountdir
cd benchmark
./test_case
