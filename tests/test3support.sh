SCRIPTPATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

while true
do
./../build/clientTest3 -f serversocket.sk -w dummyFiles,0  -R 0 -d test3dest1 -r ${SCRIPTPATH}/dummyFiles/file1,${SCRIPTPATH}/dummyFiles/file2
./../build/clientTest3 -f serversocket.sk -l ${SCRIPTPATH}/dummyFiles/file1 -c ${SCRIPTPATH}/dummyFiles/file1
./../build/clientTest3 -f serversocket.sk -W dummyFiles/bigfiles/big1,dummyFiles/bigfiles/big2 -R 0 -d test3dest2
./../build/clientTest3 -f serversocket.sk -W dummyFiles/bigfiles/randbig,dummyFiles/bigfiles/verybig3,dummyFiles/bigfiles/verybig2  -R 0 -d test3dest2
./../build/clientTest3 -f serversocket.sk -l ${SCRIPTPATH}/dummyFiles/file2 -u ${SCRIPTPATH}/dummyFiles/file2
./../build/clientTest3 -f serversocket.sk -l ${SCRIPTPATH}/dummyFiles/file2
./../build/clientTest3 -f serversocket.sk -l dummyFiles/bigfiles/big1,dummyFiles/bigfiles/big2  -c dummyFiles/bigfiles/big1,dummyFiles/bigfiles/big2
done
