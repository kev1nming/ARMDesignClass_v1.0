all:
	arm-linux-gcc -o ./bin/system ./src/main.c -I ./include -L ./lib -lapi_v4l2_arm -ljpeg -lpthread
clean:
	rm ./bin/system