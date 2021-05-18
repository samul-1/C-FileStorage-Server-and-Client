GREEN='\033[1;32m'
BG_BLUE='\x1b[44m'
RESET_COLOR='\033[0m'

if [ $# -eq 0 ]
  then
    echo "Usage: ./statistiche.sh pathToLogfile"
    exit 1
fi

echo -e "${BG_BLUE}        ${RESET_COLOR} statistiche.sh ${BG_BLUE}        ${RESET_COLOR}"
# number of operations
echo -e -n "Number of ${GREEN}read${RESET_COLOR} operations: "
grep "\"operationType\": \"READ\"" $1 | wc -l
echo -e -n "Number of ${GREEN}write${RESET_COLOR} operations: "
grep "\"operationType\": \"WRITE\"" $1 | wc -l
echo -e -n "Number of ${GREEN}lock${RESET_COLOR} operations: "
grep "\"operationType\": \"LOCK\"" $1 | wc -l
echo -e -n "Number of ${GREEN}unlock${RESET_COLOR} operations: "
grep "\"operationType\": \"UNLOCK\"" $1 | wc -l
echo -e -n "Number of ${GREEN}close${RESET_COLOR} operations: "
grep "\"operationType\": \"CLOSE\"" $1 | wc -l
echo -e -n "Number of ${GREEN}open${RESET_COLOR} operations: "
grep "\"operationType\": \"OPEN\"" $1 | wc -l
echo -e -n "Number of ${GREEN}evictions${RESET_COLOR} from cache: "
grep "\"operationType\": \"EVICTED\"" $1 | wc -l

# average processed bytes
echo -n "Average processed bytes per request: "
grep -oP '(?<="bytesProcessed": )[0-9]+' $1 | awk '{SUM += $1; COUNT += 1} END {print int(SUM/COUNT) " bytes"}'
echo ""
# requests served per thread
echo "Requests served by each worker: "
#grep -oP "\"workerTid\": [^,]*" $1 | sort | uniq -c
awk -F: '/"workerTid"/ {count[$2]++} END {for (i in count) print "· Worker" i " "count[i] " requests"}' $1