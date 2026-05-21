#!/bin/bash

RES_DIR=./benchmark/res
PLOT_DIR=./benchmark/plot
PREFIX_mine="result_"
PREFIX_baseline="result_base_"

shopt -s nullglob

bucket_for_file() {
    local filename
    filename=$(basename "$1")
    case "$filename" in
        *cpu*) echo "cpu" ;;
        *io*) echo "io" ;;
        *queue_ops*) echo "queue_ops" ;;
        *scaling*) echo "scaling" ;;
        *aging*) echo "aging" ;;
        *hetero*) echo "hetero" ;;
        *stability*) echo "stability" ;;
        *) echo "other" ;;
    esac
}

move_to_bucket() {
    local file bucket
    file="$1"
    bucket=$(bucket_for_file "$file")
    mkdir -p "$PLOT_DIR/$bucket"
    mv ./benchmark/plot/*.png "$PLOT_DIR/$bucket/"
}

move_to_bucket_baseline() {
    local file bucket
    file="$1"
    bucket=$(bucket_for_file "$file")
    mkdir -p "$PLOT_DIR/baseline/$bucket"
    mv ./benchmark/plot/*.png "$PLOT_DIR/baseline/$bucket/"
}

for file in "$RES_DIR"/${PREFIX_mine}*.txt ; do
    if [[ $(basename "$file") == ${PREFIX_baseline}* ]]; then
        continue
    fi
    python plot.py "$file"
    move_to_bucket "$file"
done

for file in "$RES_DIR"/${PREFIX_baseline}*.txt ; do
    python plot.py "$file"
    move_to_bucket_baseline "$file"
done

echo "Done"
