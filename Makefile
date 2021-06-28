#定义变量
source=main.c threadpool.c
main:$(source)
	gcc $(source) -o $@ -lpthread