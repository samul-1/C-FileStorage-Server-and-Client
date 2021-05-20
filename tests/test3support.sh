SCRIPTPATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

while true
do
./../build/clientTest3 -f serversocket.sk -w dummyFiles/smallfiles,0  -R 0 -d test3dest1 -r ${SCRIPTPATH}/dummyFiles/file1,${SCRIPTPATH}/dummyFiles/file2
./../build/clientTest3 -f serversocket.sk -l ${SCRIPTPATH}/dummyFiles/file1 -c ${SCRIPTPATH}/dummyFiles/file1
if [[ $((1 + $RANDOM % 10)) -gt 8 ]]
then
    ./../build/clientTest3 -f serversocket.sk -W dummyFiles/bigfiles/big1,dummyFiles/bigfiles/big2 -R 0 -d test3dest2
else
    ./../build/clientTest3 -f serversocket.sk -W dummyFiles/bigfiles/biggest1,dummyFiles/bigfiles/verybig3,dummyFiles/bigfiles/verybig2  -R 0 -d test3dest2
fi

./../build/clientTest3 -f serversocket.sk -l ${SCRIPTPATH}/dummyFiles/file2 -u ${SCRIPTPATH}/dummyFiles/file2
./../build/clientTest3 -f serversocket.sk -l ${SCRIPTPATH}/dummyFiles/bigfiles/big1,${SCRIPTPATH}/dummyFiles/bigfiles/big2  -c ${SCRIPTPATH}/dummyFiles/bigfiles/big1,${SCRIPTPATH}/dummyFiles/bigfiles/big2
if [[ $((1 + $RANDOM % 10)) -gt 7 ]]
then
./../build/clientTest3 -f serversocket.sk -l ${SCRIPTPATH}/dummyFiles/file2 -W dummyFiles/bigfiles/biggest2
else
./../build/clientTest3 -f serversocket.sk -l ${SCRIPTPATH}/dummyFiles/bigfiles/big1,${SCRIPTPATH}/dummyFiles/bigfiles/big2  -c ${SCRIPTPATH}/dummyFiles/bigfiles/big1,${SCRIPTPATH}/dummyFiles/bigfiles/big2
fi

done
