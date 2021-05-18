# ./../build/client -f serversocket.sk -w ../build/from,0 -R 0 -d to -p

# writes a whole dir ../build/from and saves evicted to dir `evicted`
# ./../build/client -f ../build/serversocket.sk -w ../build/from,0 -D evicted -p

# get absolute path of current directory for the -r flag (files are saved on the server using their absolute path)
SCRIPTPATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

# start server with test config file
valgrind ./../build/server test1config.txt & > server.txt
SERVER_PID=$!

# write `file1` and `file2` from subdir `dummyFiles`, then read them from
# the server and store them in subdir `test1dest1`
./../build/client -p -f serversocket.sk -W dummyFiles/file1,dummyFiles/file2  -r ${SCRIPTPATH}/dummyFiles/file1,${SCRIPTPATH}/dummyFiles/file2 -d test1dest1

# write all files and dirs in subdir `dummyFiles/rec`, then read all files from 
# the server and store them in subdir `test1dest2`
./../build/client -p -f serversocket.sk -w dummyFiles/rec,0  -R 0 -d test1dest2

# lock a file and then delete it
./../build/client -p -f serversocket.sk -l ${SCRIPTPATH}/dummyFiles/file1 -c ${SCRIPTPATH}/dummyFiles/file1

sleep 5

kill -2 $SERVER_PID
exit 0