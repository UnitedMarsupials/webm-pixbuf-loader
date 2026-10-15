#!/bin/sh
# Regenerate the sample files in data/ (needs ffmpeg with libvpx and libaom).
# Every file is 64x48 and cycles red, green, blue at 10 fps, two frames each,
# except where noted.
set -e
cd "$(dirname "$0")/data"

# Three colours, two frames each; BT.709 limited range, tagged as such.
src="color=c=red:s=64x48:r=10:d=0.2[a];color=c=0x00ff00:s=64x48:r=10:d=0.2[b];\
color=c=blue:s=64x48:r=10:d=0.2[c];[a][b][c]concat=n=3:v=1:a=0"
tag="-colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv"
conv="scale=out_color_matrix=bt709:out_range=tv"
fmt() { echo "$conv,format=$1"; }
ff() { ffmpeg -v error -y -f lavfi -i "$1" -vf "$(fmt $2)" $tag "${@:3}"; }

ffmpeg -v error -y -filter_complex "$src,$(fmt yuv420p)[o]" -map "[o]" $tag \
    -c:v libvpx -crf 10 -b:v 0 vp8.webm
ffmpeg -v error -y -filter_complex "$src,$(fmt yuv420p)[o]" -map "[o]" $tag \
    -c:v libvpx-vp9 -crf 10 -b:v 0 vp9-420.webm
ffmpeg -v error -y -filter_complex "$src,$(fmt yuv444p)[o]" -map "[o]" $tag \
    -c:v libvpx-vp9 -crf 10 -b:v 0 vp9-444.webm
ffmpeg -v error -y -filter_complex "$src,$(fmt yuv420p10le)[o]" -map "[o]" $tag \
    -c:v libvpx-vp9 -crf 10 -b:v 0 vp9-10bit.webm
ffmpeg -v error -y -filter_complex "$src,$(fmt yuv420p)[o]" -map "[o]" $tag \
    -c:v libaom-av1 -crf 10 -b:v 0 -cpu-used 8 av1.webm
# The same, but with the sequence header only in CodecPrivate (av1C).
ffmpeg -v error -y -filter_complex "$src,$(fmt yuv420p)[o]" -map "[o]" $tag \
    -c:v libaom-av1 -crf 10 -b:v 0 -cpu-used 8 \
    -bsf:v filter_units=remove_types=1 av1-noseq.webm

# Still image: one red frame.
ffmpeg -v error -y -f lavfi -i "color=c=red:s=64x48:r=10:d=0.1" \
    -vf "$(fmt yuv420p)" $tag -c:v libvpx-vp9 -crf 10 -b:v 0 -frames:v 1 \
    still.webm

# Half-transparent red, with a VP9 alpha channel.
ffmpeg -v error -y -f lavfi \
    -i "color=c=red@0.5:s=64x48:r=10:d=0.1,format=yuva420p" \
    -vf "scale=out_color_matrix=bt709:out_range=tv,format=yuva420p" $tag \
    -c:v libvpx-vp9 -crf 10 -b:v 0 -pix_fmt yuva420p -auto-alt-ref 0 \
    -frames:v 1 alpha.webm

# Two frames of half-transparent red with alpha, then two of opaque green
# without BlockAdditional alpha.
ffmpeg -v error -y -f lavfi \
    -i "color=c=red@0.5:s=64x48:r=10:d=0.2,format=yuva420p" \
    -vf "scale=out_color_matrix=bt709:out_range=tv,format=yuva420p" $tag \
    -c:v libvpx-vp9 -crf 10 -b:v 0 -pix_fmt yuva420p -auto-alt-ref 0 \
    alpha-anim1.tmp.webm
ffmpeg -v error -y -f lavfi -i "color=c=0x00ff00:s=64x48:r=10:d=0.2" \
    -vf "$(fmt yuv420p)" $tag -c:v libvpx-vp9 -crf 10 -b:v 0 \
    -auto-alt-ref 0 alpha-anim2.tmp.webm
printf "file alpha-anim1.tmp.webm\nfile alpha-anim2.tmp.webm\n" > alpha-anim.txt
ffmpeg -v error -y -f concat -i alpha-anim.txt -c copy alpha-anim.webm
rm alpha-anim1.tmp.webm alpha-anim2.tmp.webm alpha-anim.txt
