# number of operations
echo -n "Number of read operations: "
grep "\"operationType\": \"READ\"" $1 | wc -l
echo -n "Number of write operations: "
grep "\"operationType\": \"WRITE\"" $1 | wc -l
echo -n "Number of lock operations: "
grep "\"operationType\": \"LOCK\"" $1 | wc -l
echo -n "Number of unlock operations: "
grep "\"operationType\": \"UNLOCK\"" $1 | wc -l
echo -n "Number of close operations: "
grep "\"operationType\": \"CLOSE\"" $1 | wc -l
echo -n "Number of open operations: "
grep "\"operationType\": \"OPEN\"" $1 | wc -l

# todo max storage size in mbytes
# todo max file count in storage
# todo number of victim files
# todo max client count

# average processed bytes
echo -n "Average processed bytes: "
grep -oP '(?<="bytesProcessed": )[0-9]+' $1 | awk '{SUM += $1; COUNT += 1} END {print SUM/COUNT}'

# requests served per thread
echo "Requests served by each worker: "
#grep -oP "\"workerTid\": [^,]*" $1 | sort | uniq -c
awk -F: '/"workerTid"/ {count[$2]++} END {for (i in count) print "Worker" i " "count[i] " requests"}' $1