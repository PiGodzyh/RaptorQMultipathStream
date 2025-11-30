#!/bin/bash
# 生成 compile_commands.json 用于 IDE 代码跳转

echo "生成 compile_commands.json..."

# 获取 libevent 路径
LIBEVENT_PREFIX=$(brew --prefix libevent 2>/dev/null || echo "/usr/local")

cat > compile_commands.json << EOF
[
  {
    "directory": "${PWD}",
    "command": "g++ -std=c++14 -I. -I${LIBEVENT_PREFIX}/include -c network_server.cpp -o build/network_server.o",
    "file": "network_server.cpp"
  },
  {
    "directory": "${PWD}",
    "command": "g++ -std=c++14 -I. -I${LIBEVENT_PREFIX}/include -c network_client.cpp -o build/network_client.o",
    "file": "network_client.cpp"
  },
  {
    "directory": "${PWD}",
    "command": "g++ -std=c++14 -I. -I${LIBEVENT_PREFIX}/include -c server_example.cpp -o build/server_example.o",
    "file": "server_example.cpp"
  },
  {
    "directory": "${PWD}",
    "command": "g++ -std=c++14 -I. -I${LIBEVENT_PREFIX}/include -c client_example.cpp -o build/client_example.o",
    "file": "client_example.cpp"
  }
]
EOF

echo "✓ compile_commands.json 已生成"
echo "  libevent 路径: ${LIBEVENT_PREFIX}"


