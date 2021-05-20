./../build/server config/test3config.txt &
SERVER_PID=$!

pids=()
for i in {1..10}; do
    bash -c './test3support.sh' &
    echo $!
    pids+=($!)
done

sleep 5

for i in "${pids[@]}"; do
    echo ${i}
    kill -9 ${i}
    wait ${i}
done

kill -2 ${SERVER_PID}
wait ${SERVER_PID}