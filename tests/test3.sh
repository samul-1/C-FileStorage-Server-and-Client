valgrind --leak-check=full ./../build/server config/test3config.txt &
SERVER_PID=$!

pids=()
for i in {1..10}; do
    bash -c './test3support.sh' &
    pids+=($!)
done

sleep 30
kill -2 ${SERVER_PID}
wait ${SERVER_PID}

for i in "${pids[@]}"; do
    kill -9 ${i}
    wait ${i}
done
