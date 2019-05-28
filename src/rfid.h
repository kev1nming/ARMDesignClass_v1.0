/*************************************************
*头文件
*************************************************/
#include <stdio.h>
#include <fcntl.h> 
#include <unistd.h>
#include <termios.h> 
#include <sys/types.h>
#include <sys/select.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <time.h>

volatile unsigned int cardid ;         //卡片的ID
static struct timeval timeout;         //串口
#define DEV_PATH   "/dev/ttySAC1"      //设备定义


/*************************************************
* 串口参数设置
* 设置窗口参数:9600速率 
*************************************************/
void init_tty(int tty_fd)
{    
	//声明设置串口的结构体
	struct termios termios_new;
	//先清空该结构体
	bzero( &termios_new, sizeof(termios_new));
	//	cfmakeraw()设置终端属性，就是设置termios结构中的各个参数。
	cfmakeraw(&termios_new);
	//设置波特率
	termios_new.c_cflag=(B9600);
	//cfsetispeed(&termios_new, B9600);
	//cfsetospeed(&termios_new, B9600);
	//CLOCAL和CREAD分别用于本地连接和接受使能，因此，首先要通过位掩码的方式激活这两个选项。    
	termios_new.c_cflag |= CLOCAL | CREAD;
	//通过掩码设置数据位为8位
	termios_new.c_cflag &= ~CSIZE;
	termios_new.c_cflag |= CS8; 
	//设置无奇偶校验
	termios_new.c_cflag &= ~PARENB;
	//一位停止位
	termios_new.c_cflag &= ~CSTOPB;
	tcflush(tty_fd,TCIFLUSH);
	// 可设置接收字符和等待时间，无特殊要求可以将其设置为0
	termios_new.c_cc[VTIME] = 10;
	termios_new.c_cc[VMIN] = 1;
	// 用于清空输入/输出缓冲区
	tcflush (tty_fd, TCIFLUSH);
	//完成配置后，可以使用以下函数激活串口设置
	if(tcsetattr(tty_fd,TCSANOW,&termios_new) )
		printf("Setting the serial1 failed!\n");

}


/*计算校验和*/
unsigned char CalBCC(unsigned char *buf, int n)
{
	int i;
	unsigned char bcc=0;
	for(i = 0; i < n; i++)
	{
		bcc ^= *(buf+i);
	}
	return (~bcc);
}


int PiccRequest(int tty_fd)
{
	unsigned char WBuf[128], RBuf[128];
	int  ret, i,len;
	fd_set rdfd;
	
	memset(WBuf, 0, 128);
	memset(RBuf,0,128);
	WBuf[0] = 0x07;	//帧长= 7 Byte
	WBuf[1] = 0x02;	//包号= 0 , 命令类型= 0x01
	WBuf[2] = 0x41;	//命令= 'C'
	WBuf[3] = 0x01;	//信息长度= 0
	WBuf[4] = 0x52;	//请求模式:  ALL=0x52
	WBuf[5] = CalBCC(WBuf, WBuf[0]-2);		//校验和
	WBuf[6] = 0x03; 	//结束标志

	FD_ZERO(&rdfd); 
	FD_SET(tty_fd,&rdfd);
	tcflush (tty_fd, TCIFLUSH);
	write(tty_fd, WBuf, 7);
	sleep(1);
	ret = read(tty_fd, RBuf, 8);
	if (RBuf[2] == 0x00)	 	//应答帧状态部分为0 则请求成功
	{
		return 0;
	}
	return -1;
}


/*防碰撞，获取范围内最大ID*/
int PiccAnticoll(int tty_fd)
{
	unsigned char WBuf[128], RBuf[128];
	int ret, i,len;
	fd_set rdfd;;
	memset(WBuf, 0, 128);
	memset(RBuf,0,128);
	WBuf[0] = 0x08;	//帧长= 8 Byte
	WBuf[1] = 0x02;	//包号= 0 , 命令类型= 0x01
	WBuf[2] = 0x42;	//命令= 'B'
	WBuf[3] = 0x02;	//信息长度= 2
	WBuf[4] = 0x93;	//防碰撞0x93 --一级防碰撞
	WBuf[5] = 0x00;	//位计数0
	WBuf[6] = CalBCC(WBuf, WBuf[0]-2);		//校验和
	WBuf[7] = 0x03; 	//结束标志
	
	tcflush (tty_fd, TCIFLUSH);
	FD_ZERO(&rdfd);
	FD_SET(tty_fd,&rdfd);
	write(tty_fd, WBuf, 8);
	sleep(1);
	read(tty_fd, RBuf, 10);

	cardid = 0;

	if (RBuf[2] == 0x00) //应答帧状态部分为0 则获取ID 成功
	{
		cardid = (RBuf[4]<<24) | (RBuf[5]<<16) | (RBuf[6]<<8) | RBuf[7];
		usleep(100000);
		return 0;
	}
	return -1;
}


