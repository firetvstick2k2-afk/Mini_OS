#pragma once
#include <stdint.h>
#include <math.h>
#include <string.h>

// RESONIK adaptive Goertzel Morse decoder (CW-only port).
class CwDecoder {
public:
  static const int TEXT_MAX = 96;
  void begin(float sampleRate, float toneHz) {
    fs=sampleRate; blockLen=(int)(fs*0.006f+0.5f); if(blockLen<16)blockLen=16;
    blockMs=1000.0f*blockLen/fs; setTone(toneHz); reset();
  }
  void setTone(float toneHz) {
    tone=toneHz; coeff=2.0f*cosf(2.0f*(float)3.14159265358979323846f*tone/fs);
    float lo=tone-350.0f,hi=tone+350.0f;
    if(lo<150.0f)lo=tone+700.0f;
    if(hi>fs/2-150.0f)hi=tone-700.0f;
    coeffRef[0]=2.0f*cosf(2.0f*(float)3.14159265358979323846f*lo/fs);
    coeffRef[1]=2.0f*cosf(2.0f*(float)3.14159265358979323846f*hi/fs);
  }
  void reset() {
    q1=q2=dc=0; r1[0]=r1[1]=r2[0]=r2[1]=0; n=0; peak=noise=0;
    noiseRef[0]=noiseRef[1]=0; keyDown=false; pending=0; runMs=0;
    ditMs=60.0f; symLen=0; sym[0]=0; wordDone=true;
    textLen=0; text[0]=0; level=0;
  }
  void push(const int16_t* samples,int count) {
    for(int i=0;i<count;i++) {
      dc+=((float)samples[i]-dc)*0.002f; float x=(float)samples[i]-dc;
      float q0=coeff*q1-q2+x; q2=q1; q1=q0;
      for(int k=0;k<2;k++){float r0=coeffRef[k]*r1[k]-r2[k]+x;r2[k]=r1[k];r1[k]=r0;}
      if(++n>=blockLen) {
        float mag=sqrtf(fmaxf(q1*q1+q2*q2-coeff*q1*q2,0.0f))/blockLen,ref=0;
        for(int k=0;k<2;k++){
          float m=sqrtf(fmaxf(r1[k]*r1[k]+r2[k]*r2[k]-coeffRef[k]*r1[k]*r2[k],0.0f))/blockLen;
          if(noiseRef[k]==0)noiseRef[k]=m;else if(!keyDown)noiseRef[k]+=(m-noiseRef[k])*0.02f;
          float rel=m/(noiseRef[k]+1e-3f);if(rel>ref)ref=rel;r1[k]=r2[k]=0;
        }
        q1=q2=0;n=0;block(mag,ref);
      }
    }
  }
  const char* getText()const{return text;}
  int getWpm()const{return(int)(1200.0f/ditMs+0.5f);}
  bool isKeyDown()const{return keyDown;}
  uint8_t getLevel()const{return level;}
  float getTone()const{return tone;}
  void clearText(){textLen=0;text[0]=0;}
private:
  float fs=4000,tone=700,coeff=0,coeffRef[2]={0,0},r1[2]={0,0},r2[2]={0,0},noiseRef[2]={0,0};
  int blockLen=24,n=0;float blockMs=6,q1=0,q2=0,dc=0,peak=0,noise=0,runMs=0,ditMs=60;
  bool keyDown=false,wordDone=true;int pending=0;char sym[8]={0};int symLen=0;
  uint8_t level=0;char text[TEXT_MAX+1]={0};int textLen=0;
  void block(float mag,float ref){
    peak=mag>peak?mag:peak*0.995f;
    if(noise==0)noise=mag;else if(!keyDown)noise+=(mag-noise)*(mag<noise?0.05f:0.01f);
    float span=peak-noise,hi=noise+span*0.55f,lo=noise+span*0.40f;
    bool on=keyDown?(mag>lo&&mag>noise*2.2f+1.0f):(mag>hi&&mag>noise*3.2f+1.0f);
    if(on&&!keyDown&&ref>2.0f&&ref>(mag/(noise+1e-3f))*0.4f)on=false;
    float l=span>0?(mag-noise)*100.0f/span:0;level=l<0?0:l>100?100:(uint8_t)l;
    runMs+=blockMs;
    if(on!=keyDown){if(++pending>=2){float len=runMs-pending*blockMs;if(keyDown)mark(len);else space(len);keyDown=on;runMs=pending*blockMs;pending=0;}}else pending=0;
    if(!keyDown){if(symLen&&runMs>ditMs*2.5f)endLetter();if(!wordDone&&runMs>ditMs*6.0f){put(' ');wordDone=true;}}
  }
  void mark(float ms){if(ms<blockMs*1.5f||ms<ditMs*0.5f)return;bool dash=ms>ditMs*2.0f;float unit=dash?ms/3.0f:ms;if(unit>25&&unit<260)ditMs+=(unit-ditMs)*0.25f;if(symLen<(int)sizeof(sym)-1){sym[symLen++]=dash?'-':'.';sym[symLen]=0;}else symLen=sizeof(sym);wordDone=false;}
  void space(float ms){if(ms>ditMs*2.5f)endLetter();if(ms>ditMs*6.0f&&!wordDone){put(' ');wordDone=true;}}
  void endLetter(){if(!symLen)return;put(symLen<(int)sizeof(sym)?lookup(sym):'*');symLen=0;sym[0]=0;}
  void put(char c){if(c==' '&&(textLen==0||text[textLen-1]==' '))return;if(textLen>=TEXT_MAX){memmove(text,text+1,TEXT_MAX-1);textLen=TEXT_MAX-1;}text[textLen++]=c;text[textLen]=0;}
  static char lookup(const char* code){
    static const char*const letters[]={".-","-...","-.-.","-..",".","..-.","--.","....","..",".---","-.-",".-..","--","-.","---",".--.","--.-",".-.","...","-","..-","...-",".--","-..-","-.--","--.."};
    static const char*const digits[]={"-----",".----","..---","...--","....-",".....","-....","--...","---..","----."};
    static const struct{const char*code;char c;} other[]={{".-.-.-",'.'},{"--..--",','},{"..--..",'?'},{"-..-.",'/'},{"-...-",'='},{".-.-.",'+'},{"-....-",'-'},{".--.-.",'@'},{"---...",':'},{"-.--.",'('},{"-.--.-",')'},{".----.",'\''}};
    for(int i=0;i<26;i++)if(!strcmp(code,letters[i]))return'A'+i;
    for(int i=0;i<10;i++)if(!strcmp(code,digits[i]))return'0'+i;
    for(unsigned i=0;i<sizeof(other)/sizeof(other[0]);i++)if(!strcmp(code,other[i].code))return other[i].c;
    return'*';
  }
};
