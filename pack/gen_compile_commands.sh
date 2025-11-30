#!/bin/bash
# 生成 compile_commands.json 用于 IDE 代码跳转

echo "生成 compile_commands.json..."

# 获取 libRaptorQ 路径
LIBRAPTORQ_DIR="../libRaptorQ"
LIBRAPTORQ_SRC="${LIBRAPTORQ_DIR}/src"

cat > compile_commands.json << 'EOF'
[
  {
    "directory": "${PWD}",
    "command": "g++ -std=c++14 -I. -I${LIBRAPTORQ_SRC} -c rq_pack.cpp -o build/rq_pack.o",
    "file": "rq_pack.cpp"
  },
  {
    "directory": "${PWD}",
    "command": "g++ -std=c++14 -I. -I${LIBRAPTORQ_SRC} -c example.cpp -o build/example.o",
    "file": "example.cpp"
  },
  {
    "directory": "${PWD}",
    "command": "g++ -std=c++14 -I. -I${LIBRAPTORQ_SRC} -c test_pack.cpp -o build/test_pack.o",
    "file": "test_pack.cpp"
  }
]
EOF

# 替换变量
sed -i '' "s|\${PWD}|${PWD}|g" compile_commands.json
sed -i '' "s|\${LIBRAPTORQ_SRC}|${LIBRAPTORQ_SRC}|g" compile_commands.json

echo "✓ compile_commands.json 已生成"


