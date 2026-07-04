#!/bin/bash

echo "A/B" > benchmark/res/a_b_swap_mine_base.txt

for i in {1..10}
do
    echo "Run $i" >> benchmark/res/a_b_swap_mine_base.txt
    echo "Mine:" >> benchmark/res/a_b_swap_mine_base.txt
    ./bin/benchmark_heterogeneous 1
    cat benchmark/res/result_heterogeneous.txt >> benchmark/res/a_b_swap_mine_base.txt
    echo "---------------------" >> benchmark/res/a_b_swap_mine_base.txt
    echo "Base:" >> benchmark/res/a_b_swap_mine_base.txt
    ./bin/benchmark_base_heterogeneous 1
    cat benchmark/res/result_base_heterogeneous.txt >> benchmark/res/a_b_swap_mine_base.txt
    echo "---------------------" >> benchmark/res/a_b_swap_mine_base.txt
done
