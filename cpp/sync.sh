#find . \( -name .svn -prune \) -o \( -wholename './src' -prune \) -o \( -wholename './bin/txProc' -prune \) -o \( -name \*.o \) -o \( -name \*.swp \) -o \( -name \*.d \) -o -print > /tmp/sync.txt
find . \( -name .svn -prune \) -o \( -wholename './bin/txProc' -prune \) -o \( -name \*.o \) -o \( -name \*.swp \) -o \( -name \*.d \) -o -print > /tmp/sync.txt
find ../base \( -name .svn -prune \) -o \( -name \*.swp \) -o -print > /tmp/sync1.txt
rsync -av --files-from=/tmp/sync.txt . txproc:vts/cpp/txProc/
rsync -av --files-from=/tmp/sync1.txt .. txproc:vts/cpp/

