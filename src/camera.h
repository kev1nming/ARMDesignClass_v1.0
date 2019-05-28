#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/input.h>
#include <strings.h>
#include "jpeglib.h"
#include "api_v4l2.h"
#include <stdlib.h>
#include <pthread.h>

#define LCD_WIDTH 800
#define LCD_HEIGHT 480

int *p = NULL;
int is_open_camera = 0;  //打开摄像头标志
int is_video_play = 0;   //录像标志
int is_video_record = 0; //播放标志
int is_photo_take = 0;   //拍照标志
int is_photo_show = 0;   //查看标志
int video_count = 0;
int photo_count = 0;
FrameBuffer camera_buf;
int video_fd;
int photo_fd;
extern int screen_fd;
extern int event_fd;

int pause_flag = 0;

int lcd_open(void)
{
	if (screen_fd < 0)
	{
		perror("lcd open failed");
		return 0;
	}
	p = mmap(NULL, LCD_WIDTH * LCD_HEIGHT * 4,
			 PROT_READ | PROT_WRITE,
			 MAP_SHARED,
			 screen_fd,
			 0);
	if (NULL == p)
	{
		perror("lcd mmap error: ");
		return -1;
	}
	return 0;
}

int lcd_draw_point(int x, int y, int color)
{
	if (x > 800)
	{
		printf("value of X-coordinates is over: %d\n", LCD_WIDTH);
		return -1;
	}

	if (y > 480)
	{
		printf("value of Y-coordinates is over: %d\n", LCD_HEIGHT);
		return -1;
	}
	*(p + x + y * 800) = color;
	return 0;
}

void lcd_draw_bmp(const char *file_name, int x0, int y0)
{
	int fd;
	int bmp_width;
	int bmp_height;
	char buf[54];
	fd = open(file_name, O_RDONLY);
	if (fd < 0)
	{
		perror("open file error: ");
		return;
	}
	/* 读取位图头部信息 */
	read(fd, buf, 54);

	/* 宽度  */
	bmp_width = buf[18];
	bmp_width |= buf[19] << 8;
	//printf("bmp_width=%d\r\n",bmp_width);

	/* 高度  */
	bmp_height = buf[22];
	bmp_height |= buf[23] << 8;
	//printf("bmp_height=%d\r\n",bmp_height);

	int len;
	len = bmp_width * bmp_height * 3;

	//跳过 前边54个字节，这54个字节是用来存储bmp图片的相关信息的
	lseek(fd, 54, SEEK_SET);

	//从第55个字节开始，读取bmp图片的像素数组
	char bmp_buf[len];
	read(fd, bmp_buf, len); //因为这个bmp图片是24位色
	close(fd);
	int color, x, y, i = 0;
	unsigned char r, g, b;
	for (y = 0; y < bmp_height; y++)
	{
		for (x = 0; x < bmp_width; x++)
		{
			//将一个24bit //bmp图片的像素点转换为LCD的一个像素点
			b = bmp_buf[i++];
			g = bmp_buf[i++];
			r = bmp_buf[i++];
			color = (r << 16) | (g << 8) | b;
			lcd_draw_point(x + x0, bmp_height - y + y0 - 1, color);
		}
	}
}

int get_pos(int *pos_x, int *pos_y)
{
	// 打开触摸屏设备
	static struct input_event buf;
	bzero(&buf, sizeof(buf));
	while (1)
	{
		while (1)
		{
			// 循环地读取触摸屏信息
			read(event_fd, &buf, sizeof(buf));
			// 遇到 X轴 坐标事件
			if (buf.type == EV_ABS && buf.code == ABS_X)
			{
				*pos_x = buf.value;
				//printf("(%d, ", pos_x);
			}

			// 遇到 Y轴 坐标事件
			if (buf.type == EV_ABS && buf.code == ABS_Y)
			{
				*pos_y = buf.value;
				//printf("%d)\n", pos_y);
			}
			//判断手指松开
			if (buf.value == 0)
				break;
		}
	}
	return 0;
}

/****************************************************
 *函数名称:file_size_get
 *输入参数:pfile_path	-文件路径
 *返 回 值:-1		-失败
		   其他值	-文件大小
 *说	明:获取文件大小
 ****************************************************/
unsigned long file_size_get(const char *pfile_path)
{
	unsigned long filesize = -1;
	struct stat statbuff;

	if (stat(pfile_path, &statbuff) < 0)
	{
		return filesize;
	}
	else
	{
		filesize = statbuff.st_size;
	}

	return filesize;
}

/*

x			:起点
y			:起点
pjpg_path	：图片路径
pjpg_buf    ：摄像头获取的图像数据，如果你没用摄像头一般设为NULL
jpg_buf_size：摄像头数据的大小,默认设为0
*/
int lcd_draw_jpg(unsigned int x, unsigned int y, const char *pjpg_path, char *pjpg_buf, unsigned int jpg_buf_size)
{
	/*定义解码对象，错误处理对象*/
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	char g_color_buf[800 * 480 * 4];
	char *pcolor_buf = g_color_buf;
	char *pjpg;

	unsigned int i = 0;
	unsigned int color = 0;
	unsigned int count = 0;

	unsigned int x_s = x;
	unsigned int x_e;
	unsigned int y_e;

	int jpg_fd;
	unsigned int jpg_size;

	unsigned int jpg_width;
	unsigned int jpg_height;

	if (pjpg_path != NULL)
	{
		/* 申请jpg资源，权限可读可写 */
		jpg_fd = open(pjpg_path, O_RDWR);

		if (jpg_fd == -1)
		{
			printf("open %s error\n", pjpg_path);

			return -1;
		}

		/* 获取jpg文件的大小 */
		jpg_size = file_size_get(pjpg_path);

		/* 为jpg文件申请内存空间 */
		pjpg = malloc(jpg_size);

		/* 读取jpg文件所有内容到内存 */
		read(jpg_fd, pjpg, jpg_size);
	}
	else
	{
		jpg_size = jpg_buf_size;

		pjpg = pjpg_buf;
	}

	/*注册出错处理*/
	cinfo.err = jpeg_std_error(&jerr);

	/*创建解码*/
	jpeg_create_decompress(&cinfo);

	/*直接解码内存数据*/
	jpeg_mem_src(&cinfo, pjpg, jpg_size);

	/*读文件头*/
	jpeg_read_header(&cinfo, TRUE);

	/*开始解码*/
	jpeg_start_decompress(&cinfo);
	//打印获取图片的实际高度和宽度
	//printf("cinfo.output_height = %d\n,cinfo.output_width = %d\n",cinfo.output_height,cinfo.output_width);

	x_e = x_s + cinfo.output_width;
	y_e = y + cinfo.output_height;
	//printf("x_e = %d , y_e = %d\n",x_e,y_e);
	/*读解码数据*/
	while (cinfo.output_scanline < cinfo.output_height)
	{
		pcolor_buf = g_color_buf;

		/* 读取jpg一行的rgb值 */
		jpeg_read_scanlines(&cinfo, (JSAMPARRAY)&pcolor_buf, 1);

		for (i = 0; i < cinfo.output_width; i++)
		{
			/* 获取rgb值 */
			color = *(pcolor_buf + 2);
			color = color | *(pcolor_buf + 1) << 8;
			color = color | *(pcolor_buf) << 16;

			/* 显示像素点 */
			lcd_draw_point(x, y, color);

			pcolor_buf += 3;

			x++;
		}

		/* 换行 */
		y++;

		x = x_s;
		//printf("x=%d  y=%d\n",x,y);
	}

	/*解码完成*/
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);

	if (pjpg_path != NULL)
	{
		/* 关闭jpg文件 */
		close(jpg_fd);

		/* 释放jpg文件内存空间 */
		free(pjpg);
	}
	return 0;
}

void *capture(void *arg)
{
	char video_name_buf[128];
	char photo_name_buf[128];
	int i;
	while (1)
	{
		//是否打开了摄像头
		if (is_open_camera)
		{
			/* 获取摄像头的数据 */
			linux_v4l2_get_fream(&camera_buf);
			/* 在x=80,y=0,以正常大小显示摄像头的数据 */
			lcd_draw_jpg(0, 0, NULL, camera_buf.buf, camera_buf.length);
			//如果要录制视频
			if (is_video_record)
			{
				sprintf(video_name_buf, "./myvideo/video%d.jpg", video_count);

				/* 以可读可写方式新建video%d.jpg文件，并清空里面的内容 */
				video_fd = open(video_name_buf, O_RDWR | O_CREAT | O_TRUNC);

				if (video_fd)
				{
					/* 对新建的video%d.jpg文件写入数据 */
					write(video_fd, camera_buf.buf, camera_buf.length);

					/* 关闭snap.jpg文件 */
					close(video_fd);

					video_count++;
				}
			}

			//如果要播放视频
			if (is_video_play)
			{
				for (i = 0; i < video_count; i++)
				{
					while (pause_flag == 1)
					{
						usleep(1000);
					}

					//lcd_draw_jpg(0, 0, "./res/play_pause.jpg", NULL, 0);
					/* 在播放录像的时候，必须停止摄像头数据的获取 */
					is_open_camera = 0;

					sprintf(video_name_buf, "./myvideo/video%d.jpg", i);

					lcd_draw_jpg(0, 0, video_name_buf, NULL, 0);

					/* 延时30毫秒 */
					usleep(30 * 1000);

					if (is_video_play == 0)
						break;
				}

				/* 播放完毕 */
				is_video_play = 0;
				is_open_camera = 1;
				printf("camera video play end!\r\n");
			}
			//拍照
			if (is_photo_take)
			{
				sprintf(photo_name_buf, "./myphoto/photo%d.jpg", photo_count);

				/* 以可读可写方式新建video%d.jpg文件，并清空里面的内容 */
				photo_fd = open(photo_name_buf, O_RDWR | O_CREAT | O_TRUNC);

				if (photo_fd)
				{
					/* 对新建的video%d.jpg文件写入数据 */
					write(photo_fd, camera_buf.buf, camera_buf.length);

					/* 关闭snap.jpg文件 */
					close(photo_fd);

					photo_count++;
				}
				else
				{
					printf("Create photo failed\n");
				}

				is_photo_take = 0;
			}

			if (is_photo_show)
			{
				/* 在播放录像的时候，必须停止摄像头数据的获取 */
				is_open_camera = 0;

				//for (i = 0; i < photo_count; i++)
				//{
				sprintf(photo_name_buf, "./myphoto/photo%d.jpg", photo_count - 1);

				lcd_draw_jpg(0, 0, photo_name_buf, NULL, 0);

				/* 延时30毫秒 */
				sleep(3);

				//if (is_photo_show == 0)
				//	break;
				//}

				/* 播放完毕 */
				is_photo_show = 0;
				is_open_camera = 1;

				printf("camera photos play end!\r\n");
			}
		}
	}
}

