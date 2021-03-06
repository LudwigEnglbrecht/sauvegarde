set encoding iso_8859_15
set key right
set grid
set xlabel "Filesize in bytes"
set ylabel "Blocksize in bytes"
set style line 1 lw 1
set log xy
set title "Adaptive blocksize selection upon filesize"
set output "adaptive.png"
set terminal png size 1024,768 enhanced font "Verdana,10"
plot "adaptive.txt" using 1:2 title "Blocksize" with steps
