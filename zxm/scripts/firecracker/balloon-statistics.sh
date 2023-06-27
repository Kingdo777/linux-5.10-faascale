socket_location=/tmp/firecracker.socket

curl --unix-socket $socket_location -i \
    -X GET 'http://localhost/balloon/statistics' \
    -H 'Accept: application/json'
