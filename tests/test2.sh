# get absolute path of current directory for the -r flag (files are saved on the server using their absolute path)
SCRIPTPATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

valgrind --leak-check=full ./../build/server config/test2config1.txt &
SERVER_PID=$!
export SERVER_PID
bash -c 'sleep 5 && kill -1 ${SERVER_PID}' &
TIMER_PID=$!

echo "BATTERY 1 - USING LRU REPLACEMENT ALGORITHM"
echo ""

# write `big1` which is just enough to be stored
./../build/client -p -f serversocket.sk -W dummyFiles/bigfiles/big1

# write `big2` which will cause the eviction of `big1`, and store `big1` in subdir `evicted`
./../build/client -p -f serversocket.sk -W dummyFiles/bigfiles/big2  -D evicted1

# write `big3` and then read `big2` to update its ref count
./../build/client -p -f serversocket.sk -W dummyFiles/bigfiles/big3
sleep 1
./../build/client -p -f serversocket.sk -r ${SCRIPTPATH}/dummyFiles/bigfiles/big2

# write `big4`, this time `big3` will be evicted according to LRU algorithm because
# `big2` was referenced more recently
./../build/client -p -f serversocket.sk -W dummyFiles/bigfiles/big4 -D evicted1

wait $TIMER_PID
sleep 2


# --------------------------------------------------------------------------------------


valgrind --leak-check=full ./../build/server config/test2config2.txt &
SERVER_PID=$!
export SERVER_PID
bash -c 'sleep 5 && kill -1 ${SERVER_PID}' &
TIMER_PID=$!

echo "BATTERY 2 - USING LFU REPLACEMENT ALGORITHM"
echo ""

./../build/client -p -f serversocket.sk -W dummyFiles/bigfiles/big3,dummyFiles/bigfiles/big4
# read `big3` to increment its ref count
./../build/client -p -f serversocket.sk -r ${SCRIPTPATH}/dummyFiles/bigfiles/big3

# this time `big4` will be evicted, because its ref count is lower
./../build/client -p -f serversocket.sk -W dummyFiles/bigfiles/big2 -D evicted2

wait $TIMER_PID
sleep 2


# --------------------------------------------------------------------------------------


valgrind --leak-check=full ./../build/server test2config2.txt &
SERVER_PID=$!
export SERVER_PID
bash -c 'sleep 5 && kill -1 ${SERVER_PID}' &
TIMER_PID=$!

echo "BATTERY 3 - MULTIPLE EVICTED FILES"
echo ""

./../build/client -p -f serversocket.sk -W dummyFiles/bigfiles/big3,dummyFiles/bigfiles/big4

# this time both `big3` and `big4` will be evicted
./../build/client -p -f serversocket.sk -W dummyFiles/bigfiles/big1 -D evicted3

wait $TIMER_PID


exit 0