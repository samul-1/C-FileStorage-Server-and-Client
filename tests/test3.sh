GREEN='\033[1;32m'
RESET_COLOR='\033[0m'
echo -e "${GREEN}RUNNING TEST...${RESET_COLOR}"
echo -e "${GREEN}Please wait 30 seconds...${RESET_COLOR}"

./../build/server config/test3config.txt &
SERVER_PID=$!
export SERVER_PID
bash -c 'sleep 30 && kill -2 ${SERVER_PID}' &

pids=()
for i in {1..10}; do
    bash -c './test3support.sh' &
    pids+=($!)
done

sleep 30

for i in "${pids[@]}"; do
    kill -9 ${i}
    wait ${i}
done

wait ${SERVER_PID}
#sleep 1
kill -9 $(pidof clientTest3)
exit 0