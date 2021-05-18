./../build/server test1config.txt &
./../build/client -f serversocket.sk -w ../build/from,0 -R 0 -d to -p

# writes a whole dir ../build/from and saves evicted to dir `evicted`
# ./../build/client -f ../build/serversocket.sk -w ../build/from,0 -D evicted -p
