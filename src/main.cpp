#include <Arduino.h>
#include <ArduinoOTA.h>
#include <esp32_smartdisplay.h>
#include "time.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include "ff.h"
#include <SD.h>


#define SD_CS          5 // you're CS pin for SPI slave
#define DRIVE_LETTER 'S'


/* Create a type to store the required data about your file.*/
typedef  FIL file_t;

/*Similarly to `file_t` create a type for directory reading too */
typedef  FF_DIR dir_t;


// LVGL Objects
static lv_obj_t *label_cds;
static lv_obj_t *label_date;
static lv_obj_t *label_Sdate;

static lv_obj_t * ta;
static lv_obj_t * kb;


static lv_obj_t *label_ipaddress;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

WiFiMulti wifiMulti;

   static const char * kb_map[] = {"A", "Z", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
                                    "Q", "S", "D", "F", "G", "J", "K", "L", "M",  LV_SYMBOL_NEW_LINE, "\n",
                                    "W", "X", "C", "V", "B", "N", ",", ".", ":", "!", "?", "\n",
                                    LV_SYMBOL_CLOSE, " ",  " ", " ", LV_SYMBOL_OK, NULL
                                   };

    /*Set the relative width of the buttons and other controls*/
    static const lv_btnmatrix_ctrl_t kb_ctrl[] = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6,
                                                  4, 4, 4, 4, 4, 4, 4, 4, 4, 6,
                                                  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                                                  2, LV_BTNMATRIX_CTRL_HIDDEN | 2, 6, LV_BTNMATRIX_CTRL_HIDDEN | 2, 2
                                                 };

/* Initialize your Storage device and File system. */
static void fs_init(void)
{
  /* Initialisation de la carte SD */
  Serial.println(F("Init SD card... "));
  if (!SD.begin(SD_CS)) {
    Serial.println(F("FAIL"));
    for (;;); //  appui sur bouton RESET
  }
}


void Log (const char * buf) {
  Serial.println(buf ); 
}


void * fs_open (struct _lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode) {
  uint8_t flags = 0;
  void * file_p =  NULL; 
  if(mode == LV_FS_MODE_WR) flags = FA_WRITE | FA_OPEN_ALWAYS;
  else if(mode == LV_FS_MODE_RD) flags = FA_READ;
  else if(mode == (LV_FS_MODE_WR | LV_FS_MODE_RD)) flags = FA_READ | FA_WRITE | FA_OPEN_ALWAYS;

  FRESULT res = f_open((file_t*) file_p, path, flags);
  Serial.printf ("Open file : %s \r\n",path) ; 

  if(res == FR_OK) {
    f_lseek((file_t*) file_p, 0);
    return file_p;
  } else {
    return NULL;
  }
}

lv_fs_res_t fs_close (struct _lv_fs_drv_t * drv, void * file_p){
  f_close((file_t*) file_p);
  Serial.printf ("Close file \r\n") ; 

  return LV_FS_RES_OK; 
} 

lv_fs_res_t fs_read  (struct _lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br){
  FRESULT res = f_read((file_t*) file_p, buf, btr, (UINT*)br);
  Serial.printf ("Read file \r\n") ; 

  if(res == FR_OK) return LV_FS_RES_OK;
  else return LV_FS_RES_UNKNOWN;
}

lv_fs_res_t fs_write (struct _lv_fs_drv_t * drv, void * file_p, const void * buf, uint32_t btw, uint32_t * bw){
  FRESULT res = f_write((file_t*) file_p, buf, btw, (UINT*)bw);
  Serial.printf ("Write file \r\n") ; 

  if(res == FR_OK) return LV_FS_RES_OK;
  else return LV_FS_RES_UNKNOWN;
}

lv_fs_res_t fs_seek  (struct _lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence){
    f_lseek((file_t*) file_p, pos);
    Serial.printf ("Seek file \r\n") ; 

    return LV_FS_RES_OK;
}

lv_fs_res_t fs_tell  (struct _lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p){
  *pos_p = f_tell(((file_t *)file_p));
  Serial.printf ("Tell file  \r\n") ; 

  return LV_FS_RES_OK;
}

void * fs_dir_open (struct _lv_fs_drv_t * drv, const char * path){
  void * dir_p = NULL; 
  FRESULT res = f_opendir((dir_t*)dir_p, path);
  Serial.printf ("Open dir   : %s \r\n",path) ; 

  if(res == FR_OK) return dir_p;
  else return NULL;
}

lv_fs_res_t fs_dir_read (struct _lv_fs_drv_t * drv, void * dir_p, char * fn){
  FRESULT res;
  FILINFO fno;
  Serial.printf ("Read dir \r\n") ; 

  fn[0] = '\0';

    do {
      res = f_readdir((dir_t*)dir_p, &fno);
      if(res != FR_OK) return LV_FS_RES_UNKNOWN;

    if(fno.fattrib & AM_DIR) {
      fn[0] = '/';
      strcpy(&fn[1], fno.fname);
    }
    else strcpy(fn, fno.fname);

    } while(strcmp(fn, "/.") == 0 || strcmp(fn, "/..") == 0);

    return LV_FS_RES_OK;

}
lv_fs_res_t fs_dir_close (struct _lv_fs_drv_t * drv, void * dir_p){
  f_closedir((dir_t*)dir_p);
  Serial.printf ("Close dir  file  \r\n") ; 

  return LV_FS_RES_OK;
}

void lv_fs_if_init(void)
{
    /*----------------------------------------------------
     * Initialize your storage device and File System
     * -------------------------------------------------*/
    fs_init();

    /*---------------------------------------------------
     * Register the file system interface  in LittlevGL
     *--------------------------------------------------*/

    /* Add a simple drive to open images */
    lv_fs_drv_t fs_drv;                         /*A driver descriptor*/
    lv_fs_drv_init(&fs_drv);

    /*Set up fields...*/
    fs_drv.letter = DRIVE_LETTER;
    fs_drv.open_cb = fs_open;
    fs_drv.close_cb = fs_close;
    fs_drv.read_cb = fs_read;
    fs_drv.write_cb = fs_write;
    fs_drv.seek_cb = fs_seek;
    fs_drv.tell_cb = fs_tell;
       
    fs_drv.dir_close_cb = fs_dir_close;
    fs_drv.dir_open_cb = fs_dir_open;
    fs_drv.dir_read_cb = fs_dir_read;

    lv_log_register_print_cb(Log) ; 

    lv_fs_drv_register(&fs_drv);
}


String printLocalTime()
{
  time_t rawtime;
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
   return(String(""));
  }
  char timeStringBuff[50]; //50 chars should be enough
  strftime(timeStringBuff, sizeof(timeStringBuff), "%A, %B %d %Y %H:%M:%S", &timeinfo);
  //print like "const char*"
  //Serial.println(timeStringBuff);

  //Optional: Construct String object 
  return (String(timeStringBuff));
}

void display_update()
{
  char buffer[32];
  itoa(millis(), buffer, 10);
  lv_label_set_text(label_date, buffer);
  lv_label_set_text(label_ipaddress, WiFi.localIP().toString().c_str());
  lv_label_set_text(label_cds, String(smartdisplay_get_light_intensity()).c_str());
  if ( WiFi.isConnected()  )
  {
    lv_label_set_text(label_Sdate,printLocalTime().c_str());
  }
}

void btn_event_cb(lv_event_t *e)
{
  const std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);

  auto code = lv_event_get_code(e);
  auto btn = lv_event_get_target(e);
  if (code == LV_EVENT_CLICKED)
  {
    static uint8_t cnt = 0;
    cnt++;

    smartdisplay_beep(1000, 50);

    auto label = lv_obj_get_child(btn, 0);
    lv_label_set_text_fmt(label, "Button: %d", cnt);
  }
}

void btn2_event_cb(lv_event_t *e)
{
  const std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);

  auto code = lv_event_get_code(e);
  auto btn2 = lv_event_get_target(e);
  if (code == LV_EVENT_CLICKED)
  {
    static uint8_t keyboard = 0;
    keyboard ++;
    keyboard %=2  ; 


    smartdisplay_beep(1000, 50);

    auto label2 = lv_obj_get_child(btn2, 0);
    if (keyboard == 0)
    {
      lv_label_set_text_fmt(label2, "keyboard");
      lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ta, LV_OBJ_FLAG_HIDDEN);


    }
    else
    {
      lv_label_set_text_fmt(label2, "masquer");
      lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ta, LV_OBJ_FLAG_HIDDEN);

    }

  }
}

void mainscreen()
{
  const std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);

  // Clear screen
  lv_obj_clean(lv_scr_act());


  // Create a buttom
  auto btn = lv_btn_create(lv_scr_act());
  lv_obj_set_pos(btn, 10, 10);
  lv_obj_set_size(btn, 100, 50);
  lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_ALL, NULL);

  auto btn2 = lv_btn_create(lv_scr_act());
  lv_obj_set_pos(btn2, 10, 10);
  lv_obj_set_size(btn2, 100, 50);
  lv_obj_add_event_cb(btn2, btn2_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_align(btn2, LV_ALIGN_TOP_RIGHT, -10, 10);


  // Set label to Button
  auto label = lv_label_create(btn);
  lv_label_set_text(label, "Button");
  lv_obj_center(label);

    // Set label to Button
  auto label2 = lv_label_create(btn2);
  lv_label_set_text(label2, "keyboard");
  lv_obj_center(label2);


  // Create a label for the date
  label_date = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_font(label_date, &lv_font_montserrat_22, LV_STATE_DEFAULT);
  lv_obj_align(label_date, LV_ALIGN_TOP_MID, 0, 150);

  // Create a label for the IP Address
  label_ipaddress = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_font(label_ipaddress, &lv_font_montserrat_22, LV_STATE_DEFAULT);
  lv_obj_align(label_ipaddress, LV_ALIGN_TOP_MID, 0, 70);

  label_Sdate = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_font(label_Sdate, &lv_font_montserrat_14, LV_STATE_DEFAULT);
  lv_obj_align(label_Sdate, LV_ALIGN_TOP_MID, 0, 100);

  // Create a label for the CDS Sensor
  label_cds = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_font(label_cds, &lv_font_montserrat_22, LV_STATE_DEFAULT);
  lv_obj_align(label_cds, LV_ALIGN_TOP_MID, 0, 120);

    kb = lv_keyboard_create(lv_scr_act());
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);



    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, kb_map, kb_ctrl);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);

    /*Create a text area. The keyboard will write here*/
    ta = lv_textarea_create(lv_scr_act());
    lv_obj_add_flag(ta, LV_OBJ_FLAG_HIDDEN);

    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_set_size(ta, lv_pct(90), 40);
    lv_obj_add_state(ta, LV_STATE_FOCUSED);

    lv_keyboard_set_textarea(kb, ta);

}

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(115200);

  smartdisplay_init();
  lv_fs_if_init();

  WiFi.mode(WIFI_STA); //Optional

  
  wifiMulti.addAP("testAP", "testPassword");



  if(wifiMulti.run() != WL_CONNECTED) {
    Serial.println("WiFi not connected!");
    delay(1000);
  }

  // Set the time servers
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  mainscreen();
}

void loop()
{
  // put your main code here, to run repeatedly:

  // Red if no wifi, otherwise green
  bool connected = WiFi.isConnected();
  smartdisplay_set_led_color(connected ? lv_color32_t({.ch = {.green = 0xFF}}) : lv_color32_t({.ch = {.red = 0xFF}}));

  ArduinoOTA.handle();

  display_update();
  lv_timer_handler();
}