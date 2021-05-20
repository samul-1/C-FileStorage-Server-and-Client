./../build/server config/test3config.txt &
SERVER_PID=$!

pids=()
for i in {1..10}; do
    bash -c './test3support.sh' &
    pids+=($!)
done

kill -2 ${SERVER_PID}
wait ${SERVER_PID}

sleep 5
for i in "${pids[@]}"; do
    kill -9 ${i}
    wait ${i}
done

kill -9 pidof clientTest3
