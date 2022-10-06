# Colors for echoing
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

# Check correctness
num=$((0))
cor=$((0))
path=~/eecs583/hw2/cmake-build-debug/HW2/
cd ~/eecs583/hw2/benchmarks/correctness || exit
for file in ./*.c; do
  module=${file##*/}
  module=${module%.c}
  ./run.sh "$module" &> $(pwd)/"$module".log
  num=$(($num+1))
  if grep -c "PASS" $(pwd)/"$module".log > /dev/null; then
    cor=$(($cor+1))
    echo -e "$module" "${GREEN}PASSED${NC} => " $(grep "real" "$module".log)
  else
    echo -e "$module" "${RED}PASSED${NC}"
  fi
done

cd ~/eecs583/hw2/benchmarks/performance || exit
for file in ./*.c; do
  module=${file##*/}
  module=${module%.c}
  ./run.sh "$module" &> $(pwd)/"$module".log
  num=$(($num+1))
  if grep -c "PASS" $(pwd)/"$module".log > /dev/null; then
    cor=$(($cor+1))
    echo -e "$module" "${GREEN}PASSED${NC} => " $(grep "real" "$module".log)
  else
    echo -e "$module" "${RED}FAILED${NC}"
  fi
done
echo Results: $cor/$num