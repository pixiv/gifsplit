#!/bin/bash

set -e

cd $(dirname $0)

gifsplit=./gifsplit
convert=convert

compareimg() {
    cmp <(convert "$1" -background none -alpha Background -depth 8 rgba:-) \
        <(convert "$2" -background none -alpha Background -depth 8 rgba:-) || \
            return 1
    return 0
}

testgif() {
    gif="$1"
    echo "Testing $gif..."

    tmp=$(mktemp -d)

    $gifsplit "$gif" "$tmp/out-" >"$tmp/stdout"
    echo -n "  Gifsplit done. "
    $convert "$gif" -alpha set -background none -coalesce \
        -depth 8 +adjoin -quality 10 "$tmp/ref.png"
    echo -n "Ref split done. Comparing frames... "

    frames=$(ls "$tmp"/out-*.png | wc -l)
    refframes=$(ls "$tmp"/ref*.png | wc -l)

    if [ "$frames" != "$refframes" ] ; then
        echo "Frame count mismatch! out=$frames ref=$refframes"
        echo "Temp dir: $tmp"
        return 1
    fi

    if [ "$frames" == "1" ] ; then
        if ! compareimg "$tmp/out-000000.png" "$tmp/ref.png" ; then
            echo "Frame 0 mismatch"
            echo "Temp dir: $tmp"
            return 1
        fi
        rm -rf "$tmp"
        return 0
    fi

    for frame in $(seq 0 $((frames-1))); do
        out="$tmp/out-$(printf "%06d" $frame).png"
        ref="$tmp/ref-$frame.png"
        if ! compareimg "$out" "$ref" ; then
            echo "Frame $frame mismatch: $out vs. $ref"
            echo "Temp dir: $tmp"
            return 1
        fi
    done

    rm -rf "$tmp"
    echo "OK"
    return 0
}

for gif in testdata/*.gif; do
    testgif $gif
done
