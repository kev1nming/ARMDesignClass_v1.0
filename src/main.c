#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <linux/input.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include "camera.h"
#include "rfid.h"

#define GARAGE_MAX 100
#define PARKING_FEE 1
#define NUM_DIP_WEIGHT 30

int screen_fd;
int tty_fd;
int event_fd;

int reg_lock = 0;
int unreg_lock = 0;

int func_flag = 0; //which func page to show 0:main 1:manager 2:rec/replay 3:rec ctl 4:rep ctl
int x = 0;
int y = 0;
int dis_nums = 0;
char nums_path[10][15];
int fee_hold = 0;
static int garage_free = GARAGE_MAX;
int record_flag = 0;

pthread_t RFID_thread;
pthread_t num_display_thread;
pthread_t t_camera_id;

//extern unsigned int cardid;
extern int get_pos(int *pos_x, int *pos_y);

extern int lcd_open(void);
extern void lcd_draw_bmp(const char *file_name, int x0, int y0);
extern int lcd_draw_jpg(unsigned int x, unsigned int y, const char *pjpg_path, char *pjpg_buf, unsigned int jpg_buf_size);

extern void init_tty(int tty_fd);
extern int PiccRequest(int tty_fd);
extern int PiccAnticoll(int tty_fd);

void init(void);
int db_tools(int flag, int cardid_chk); // flag:0 calc ;1 add ;2 rm
void reg();
void unreg();
void display_nums(int x, int y, int num);
void start_camera(int *x, int *y);

void *touch_handler(void *args);
void *touch_get_handler(void *args);
void *num_display_handler(void *args);
void *RFID_handler(void *args);
/* void *debug_handler(void *args); */

int main(void)
{
    fprintf(stdout, "preinit\n");
    init();
    int stat;
    pthread_t touch_thread;
    pthread_t touch_get_thread;

    pthread_t debug;
    stat = pthread_create(&touch_thread, NULL, touch_handler, NULL);
    fprintf(stdout, "touch%d\n", stat);
    stat =
        fprintf(stdout, "display%d\n", stat);
    stat = pthread_create(&RFID_thread, NULL, RFID_handler, NULL);
    fprintf(stdout, "rfid%d\n", stat);
    stat = pthread_create(&touch_get_thread, NULL, touch_get_handler, NULL);
    fprintf(stdout, "gettouch%d\n", stat);
    /*     pthread_create(&debug, NULL, debug_handler, NULL); */

    pthread_join(touch_thread, NULL);
    return 0;
}

void init()
{
    int i;
    screen_fd = open("/dev/fb0", O_RDWR);
    tty_fd = open("/dev/ttySAC1", O_RDWR | O_NOCTTY | O_NONBLOCK);
    event_fd = open("/dev/input/event0", O_RDWR);
    fprintf(stdout, "dev_inited");
    char nums_init[] = {"res/num0.jpg"};
    for (i = 0; i < 10; i++)
    {
        strcpy(nums_path[i], nums_init);
        nums_init[7]++;
    }
    fprintf(stdout, "num_inited");

    lcd_open(); //打开lcd显示屏
    init_tty(tty_fd);
    fprintf(stdout, "init done\n");

    /* 打开摄像头设备 */
    linux_v4l2_device_init("/dev/video0");
    usleep(10000);
    /* 启动摄像头捕捉 */
    linux_v4l2_start_capturing();
}

int db_tools(int flag, int cardid_chk)
{
    int time_now;
    char time_now_char[20];
    long time_reg;
    char time_reg_char[20];

    char cid[20];
    char path[30];
    int dbfile_fd;
    //FILE * dbfile_pt;

    int fee = 0;
    sprintf(cid, "%x", cardid_chk); //card id to str
    strcpy(path, "db/");
    strcat(path, cid);
    if (flag == 0) //nofile:-1 ;
    {
        dbfile_fd = open(path, O_RDONLY);
        if (dbfile_fd != -1)
        {
            time_now = time(NULL);
            read(dbfile_fd, time_reg_char, 20);
            close(dbfile_fd);
            fprintf(stdout, "time_reg_char:%s\n", time_reg_char);
            time_reg = strtol(time_reg_char, NULL, 10);
            fee = (time_now - time_reg) * PARKING_FEE;
            fprintf(stdout, "fee checked:%d\n", fee);
            return fee;
        }
        else
        {
            return -1;
        }
    }
    else if (flag == 1)
    {
        dbfile_fd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        time_now = time(NULL);
        sprintf(time_reg_char, "%d", time_now); //reg  //write time
        write(dbfile_fd, time_reg_char, sizeof(time_reg_char));
        close(dbfile_fd);
        //lcd_draw_jpg(); //reg done!
        fprintf(stdout, "id:%d reged\n", cardid_chk);
        return 0;
    }
    else if (flag == 2)
    {
        remove(path);
        cardid = 0;
        return 0;
    }
}

void reg()
{

    if (reg_lock == 1)
    {
        return;
    }
    reg_lock = 1;
    char photo_name_buf[128];
    static int cardid_reg;
    cardid_reg = cardid; //hold card

    if (cardid_reg != 0) //check card
    {
        fprintf(stdout, "card id to reg:%x\n", cardid_reg);

        if (db_tools(0, cardid_reg) != -1) //check regfile
        {
            //lcd_draw_jpg(); //have been reg!
            fprintf(stdout, "id:%x already reged\n", cardid_reg);
            lcd_draw_jpg(300, 110, "./res/already_reg.jpg", NULL, 0);
        }
        else
        {
            if (garage_free <= 0) //check garage
            {
                //lcd_draw_jpg(); // no space
                fprintf(stdout, "no more garage\n");
                lcd_draw_jpg(300, 110, "./res/full.jpg", NULL, 0);
            }
            else
            {
                pthread_cancel(RFID_thread);
                cardid = 0;
                db_tools(1, cardid_reg);
                //lcd_draw_jpg(); //reg done!
                garage_free--;
                lcd_draw_jpg(300, 110, "./res/signupsuccess.jpg", NULL, 0);
                fprintf(stdout, "id:%d reged\n", cardid_reg);

                linux_v4l2_stop_capturing();
                linux_v4l2_start_capturing();
                pthread_create(&t_camera_id, NULL, capture, NULL);
                is_open_camera = 1;
                usleep(10000);
                is_photo_take = 1;
                printf("t_camera_id:%d\n", t_camera_id);
                sleep(1);
                pthread_cancel(t_camera_id);

                sleep(1);
                lcd_draw_jpg(300, 110, "./res/success_in.jpg", NULL, 0);
                pthread_create(&RFID_thread, NULL, RFID_handler, NULL);
                fee_hold = 0;
            }
        }
    }
    else
    {
        //nocard
        //lcd_draw_jpg(pos_x,pos_y,"./res/nocard.jpg",NULL,0);
        lcd_draw_jpg(300, 110, "./res/nocard.jpg", NULL, 0);
    }
    reg_lock = 0;
}

void unreg()
{
    static int cardid_unreg;

    cardid_unreg = cardid; //hold card
    if (cardid_unreg != 0) //check card
    {
        if (db_tools(0, cardid_unreg) == -1) //check regfile
        {
            //lcd_draw_jpg(); //have not been reg!
            fprintf(stdout, "id:%x have not been reged\n", cardid_unreg);
            lcd_draw_jpg(300, 110, "./res/nosignup.jpg", NULL, 0);
        }
        else
        {
            pthread_cancel(RFID_thread);
            fee_hold = db_tools(0, cardid_unreg);
            db_tools(2, cardid_unreg);
            if (garage_free < 100)
                garage_free++; //relese garage
            fprintf(stdout, "id:%d unreged\n", cardid_unreg);
            //lcd_draw_jpg();
            linux_v4l2_stop_capturing();
            linux_v4l2_start_capturing();
            pthread_create(&t_camera_id, NULL, capture, NULL);
            is_open_camera = 1;
            usleep(10000);
            is_photo_take = 1;
            printf("t_camera_id:%d\n", t_camera_id);
            sleep(1);
            pthread_cancel(t_camera_id);
            lcd_draw_jpg(300, 110, "./res/deletesuccess.jpg", NULL, 0);
            pthread_create(&RFID_thread, NULL, RFID_handler, NULL);
            x = 0;
            y = 0;

            sleep(2);
        }
    }
    else
    {
        //nocard!
        //lcd_draw_jpg(pos_x,pos_y,"./res/nocard.jpg",NULL,0);
        lcd_draw_jpg(300, 110, "./res/nocard.jpg", NULL, 0);
    }
}

void display_nums(int x, int y, int num)
{
    int i;
    if (num == 0)
    {
        lcd_draw_jpg(x, y, nums_path[0], NULL, 0);
        return;
    }
    x += NUM_DIP_WEIGHT;
    for (i = 0; num > 0; i++)
    {
        x -= NUM_DIP_WEIGHT;
        lcd_draw_jpg(x, y, nums_path[num % 10], NULL, 0);
        num /= 10;
    }
    return;
}

void *touch_handler(void *args)
{

    int ui_flag = 0;
    /*     x = touch_x;
    y = touch_y; */
    fprintf(stdout, "touch_handler thread start\n");
    //lcd_draw_jpg(0, 0, "res/main.jpg", NULL, 0);
    while (1)
    {

        //get_pos(&x,&y);
        //printf("%d,%d\n",x,y);
        if (ui_flag == 0) //首页
        {
            lcd_draw_jpg(0, 0, "res/main.jpg", NULL, 0);
            if (x > 690 && x < 820 && y > 335 && y < 400)
            {
                printf("Manager\n");
                ui_flag = 1;
            }
            if (x > 690 && x < 820 && y > 425 && y < 488)
            {
                printf("Camera\n");
                ui_flag = 2;
            }
        }

        else if (ui_flag == 1) //管理
        {
            //显示数字
            reg();
            usleep(100000);
            pthread_create(&num_display_thread, NULL, num_display_handler, NULL);

            lcd_draw_jpg(0, 0, "./res/manage.jpg", NULL, 0);

            if (x > 690 && x < 770 && y > 40 && y < 90) //返回首页
            {
                pthread_cancel(num_display_thread);
                dis_nums = 0;
                lcd_draw_jpg(0, 0, "./res/main.jpg", NULL, 0);
                printf("Back\n");
                ui_flag = 0;

                continue;
            }

            if (x > 585 && x < 733 && y > 380 && y < 454)
            {
                unreg();
            }
        }
        else if (ui_flag == 2) //监控
        {

            printf("Starting camera\n");
            lcd_draw_jpg(0, 0, "./res/loading.jpg", NULL, 0);
            sleep(1);
            lcd_draw_jpg(0, 0, "./res/rec_play.jpg", NULL, 0);
            //sleep(1);
            x = y = 0;
            sleep(1);
            start_camera(&x, &y);

            lcd_draw_jpg(0, 0, "./res/main.jpg", NULL, 0);
            ui_flag = 0;
        }
    }
}

void start_camera(int *x, int *y)
{
    pthread_create(&t_camera_id, NULL, capture, NULL);
    is_open_camera = 1;
    printf("t_camera_id:%d\n", t_camera_id);
    while (1)
    {
        if (*x > 690 && *x < 770 && *y > 40 && *y < 90) //返回
        {
            *x = *y = 0;
            printf("返回主界面\n");
            is_open_camera = 0;
            // 取消线程
            pthread_cancel(t_camera_id);
            printf("t_camera_id:%d\n", t_camera_id);
            //lcd_draw_jpg(0,0,"./res/main.jpg", NULL, 0);
            break;
        }
        else if (*x > 660 && *x < 790 && *y > 150 && *y < 216) //拍照
        {
            *x = *y = 0;
            printf("拍照\n");
            /*         is_open_camera = 1;
            is_vedio_record = 0;
            is_video_play = 0; */
            is_photo_take = 1;
        }
        else if (*x > 660 && *x < 790 && *y > 234 && *y < 297) //查看
        {
            *x = *y = 0;
            printf("查看:%d\n", photo_count);
            is_photo_show = 1;
            // is_vedio_record = 1;
        }

        else if (*x > 660 && *x < 790 && *y > 320 && *y < 381) //录像
        {
            *x = *y = 0;
            printf("录像\n");
            if (record_flag == 0)
            {
                is_video_record = 1;
                record_flag = 1;
            }

            else if (record_flag == 1)
            {
                is_video_record = 0;
                record_flag = 0;
            }
        }
        else if (*x > 660 && *x < 790 && *y > 405 && *y < 470) //播放
        {
            *x = *y = 0;
            printf("播放\n");
            is_video_play = 1;
            lcd_draw_jpg(0, 0, "./res/video.jpg", NULL, 0);
            lcd_draw_jpg(650, 390, "./res/play_pause.jpg", NULL, 0);

            while (is_video_play == 1)
            {
                if (*x > 660 && *x < 790 && *y > 405 && *y < 470) //暂停
                {
                    *x = *y = 0;
                    printf("暂停\n");

                    // is_open_camera = 0;
                    //is_video_play = 0;
                    if (pause_flag == 0)
                    {
                        pause_flag = 1;
                        lcd_draw_jpg(640, 385, "./res/play_recover.jpg", NULL, 0);
                    }

                    else if (pause_flag == 1)
                    {
                        pause_flag = 0;
                        lcd_draw_jpg(650, 390, "./res/play_pause.jpg", NULL, 0);
                    }

                    //lcd_draw_jpg(0,0,"./res/play_pause.jpg", NULL, 0);
                    continue;
                }
                if (*x > 690 && *x < 770 && *y > 40 && *y < 90) //返回
                {
                    *x = *y = 0;
                    printf("返回主界面\n");
                    is_open_camera = 0;
                    // 取消线程
                    pthread_cancel(t_camera_id);
                    printf("t_camera_id:%d\n", t_camera_id);
                    //lcd_draw_jpg(0,0,"./res/main.jpg", NULL, 0);
                    break;
                }
            }
            lcd_draw_jpg(0, 0, "./res/rec_play.jpg", NULL, 0);
        }
    }
}

void *touch_get_handler(void *args)
{
    /* fprintf(stdout,"get_touch_handler thread start\n");
    get_pos(&touch_x, &touch_y); */
    struct input_event buf;
    while (1)
    {
        while (1)
        {
            read(event_fd, &buf, sizeof(buf));
            if (buf.type == EV_ABS && buf.code == ABS_X)
            {
                printf("[x:%d,\t", buf.value);
                x = buf.value;
            }
            if (buf.type == EV_ABS && buf.code == ABS_Y)
            {
                printf("y:%d]\n", buf.value);
                y = buf.value;
            }
            if (buf.value == 0)
            {
                printf("exit touch...[x:%d,\ty:%d]\n", x, y);
                break;
            }
        }
    }
}

void *num_display_handler(void *args)
{
    dis_nums = 1;
    while (dis_nums)
    {
        display_nums(477, 205, GARAGE_MAX);
        display_nums(477, 293, garage_free);
        if (cardid != 0)
        {
            if (fee_hold != 0)
            {
                display_nums(477, 381, fee_hold);
            }
            else
            {
                display_nums(477, 381, db_tools(0, cardid));
            }
        }
        else
        {
            display_nums(477, 381, 0);
        }
        usleep(700000);
    }
}


void *RFID_handler(void *args)
{
    while (1)
    {

        /*请求天线范围的卡*/
        if (PiccRequest(tty_fd))
        {
            fprintf(stdout, "The request failed!\n");
            continue;
        }
        /*进行防碰撞，获取天线范围内最大的ID*/
        if (PiccAnticoll(tty_fd))
        {
            fprintf(stdout, "Couldn't get card-id!\n");
            continue;
        }
        fprintf(stdout, "card ID = %x\n", cardid);
        fprintf(stdout, "%d,%d,%d,%d,%d\n", func_flag, x, y, fee_hold, garage_free);
    }
}

