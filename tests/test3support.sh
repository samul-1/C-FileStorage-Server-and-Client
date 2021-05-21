SCRIPTPATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

while true
do
build/clientTest3 -f serversocket.sk -w tests/dummyFiles/smallfiles,0  -R 0 -d tests/test3dest1 -r ${SCRIPTPATH}/dummyFiles/file1,${SCRIPTPATH}/dummyFiles/file2
build/clientTest3 -f serversocket.sk -l ${SCRIPTPATH}/dummyFiles/file1 -c ${SCRIPTPATH}/dummyFiles/file1
if [[ $((1 + $RANDOM % 10)) -gt 8 ]]
then
    build/clientTest3 -f serversocket.sk -W tests/dummyFiles/bigfiles/big1,tests/dummyFiles/bigfiles/big2 -R 0 -d tests/test3dest2 -r ${SCRIPTPATH}/dummyFiles/bigfiles/big1
else
    build/clientTest3 -f serversocket.sk -W tests/dummyFiles/bigfiles/biggest1,tests/dummyFiles/bigfiles/verybig3,tests/dummyFiles/bigfiles/verybig2  -R 0 -d tests/test3dest2
fi

build/clientTest3 -f serversocket.sk -l ${SCRIPTPATH}/dummyFiles/file2 -u ${SCRIPTPATH}/dummyFiles/file2 -r ${SCRIPTPATH}/dummyFiles/file1,${SCRIPTPATH}/dummyFiles/file2
build/clientTest3 -f serversocket.sk -l ${SCRIPTPATH}/dummyFiles/bigfiles/big1,${SCRIPTPATH}/dummyFiles/bigfiles/big2  -c ${SCRIPTPATH}/dummyFiles/bigfiles/big1,${SCRIPTPATH}/dummyFiles/bigfiles/big2
if [[ $((1 + $RANDOM % 10)) -gt 7 ]]
then
build/clientTest3 -f serversocket.sk -l ${SCRIPTPATH}/dummyFiles/file2 -W tests/dummyFiles/bigfiles/biggest2 -r ${SCRIPTPATH}/dummyFiles/bigfiles/big2
else
build/clientTest3 -f serversocket.sk -l ${SCRIPTPATH}/dummyFiles/bigfiles/big1,${SCRIPTPATH}/dummyFiles/bigfiles/big2  -c ${SCRIPTPATH}/dummyFiles/bigfiles/big1,${SCRIPTPATH}/dummyFiles/bigfiles/big2
fi

done
