GREEN='\033[1;32m'
RESET_COLOR='\033[0m'
# get absolute path of current directory for the -r flag (files are saved on the server using their absolute path)
SCRIPTPATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

valgrind --leak-check=full ./../build/server config/test2config1.txt &
SERVER_PID=$!
export SERVER_PID
bash -c 'sleep 3 && kill -1 ${SERVER_PID}' &
TIMER_PID=$!

echo -e "${GREEN}BATTERY 1 - USING LRU REPLACEMENT ALGORITHM${RESET_COLOR}"
echo ""

# write `big2` and `randbig` which both fit in the storage
./../build/client -p -f serversocket.sk -W dummyFiles/bigfiles/big2,dummyFiles/bigfiles/randbig

# make sure the clock ticks
sleep 1
# read `big2` to update its last ref time
./../build/client -p -f serversocket.sk -r ${SCRIPTPATH}/dummyFiles/bigfiles/big2

# write `big4` which will cause the eviction of `randbig`, which is the least recently used,
# and store `randbig` in subdir `evicted1`
./../build/client -p -f serversocket.sk -W dummyFiles/bigfiles/big4  -D evicted1


wait $SERVER_PID
wait $TIMER_PID
sleep 2


# --------------------------------------------------------------------------------------


valgrind --leak-check=full ./../build/server config/test2config2.txt &
SERVER_PID=$!
export SERVER_PID
bash -c 'sleep 3 && kill -1 ${SERVER_PID}' &
TIMER_PID=$!

echo ""
echo -e "${GREEN}BATTERY 2 - USING LFU REPLACEMENT ALGORITHM${RESET_COLOR}"
echo ""

# write `big2` and `randbig` which both fit in the storage
./../build/client -p -f serversocket.sk -W dummyFiles/bigfiles/big2,dummyFiles/bigfiles/randbig


# read `randbig` to update its ref count
./../build/client -p -f serversocket.sk -r ${SCRIPTPATH}/dummyFiles/bigfiles/randbig
# read `randbig` to update its ref count
./../build/client -p -f serversocket.sk -r ${SCRIPTPATH}/dummyFiles/bigfiles/randbig
# read `big2` to update its ref count
./../build/client -p -f serversocket.sk -r ${SCRIPTPATH}/dummyFiles/bigfiles/big2

# now `randbig` has a higher count than `rand2`

# write `big4` which will cause the eviction of `big2`, which is the least frequently used,
# and store `big2` in subdir `evicted2`
./../build/client -p -f serversocket.sk -W dummyFiles/bigfiles/big4  -D evicted2
wait $SERVER_PID
wait $TIMER_PID
sleep 2


# # --------------------------------------------------------------------------------------


valgrind --leak-check=full ./../build/server config/test2config2.txt &
SERVER_PID=$!
export SERVER_PID
bash -c 'sleep 3 && kill -1 ${SERVER_PID}' &
TIMER_PID=$!

echo ""
echo -e "${GREEN}BATTERY 3 - MULTIPLE EVICTED FILES${RESET_COLOR}"
echo ""

# write `big2` and `randbig` which both fit in the storage
./../build/client -p -f serversocket.sk -W dummyFiles/bigfiles/big2,dummyFiles/bigfiles/randbig


# write `big1` which will cause the eviction of both `big2` and `randbig`, and store both in subdir `evicted3`
./../build/client -p -f serversocket.sk -W dummyFiles/bigfiles/big1  -D evicted3

wait $SERVER_PID
wait $TIMER_PID


exit 0