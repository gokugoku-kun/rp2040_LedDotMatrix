import cv2
import sys

BIT_SIZE = 8

args = sys.argv

# 画像読み込み
img = cv2.imread(args[1])

scal = 64/img.shape[1]

resizeimg=cv2.resize(img,(int(img.shape[1]*scal),int(img.shape[0]*scal)))

# img[top : bottom, left : right]
# サンプル1の切り出し、保存
col = 64*3
img1 = resizeimg[col-64 : col, 0: 64]
cv2.imwrite(args[2], img1)

frame = img1
print('{')
for v in range( frame.shape[ 0 ] ):
    print('{',end='')
    for h in range ( frame.shape[ 1 ] ):
        b = frame[v][h][0]
        g = frame[v][h][1]
        r = frame[v][h][2]
        bl = min((b*(BIT_SIZE+1))//255,BIT_SIZE)
        gl = min((g*(BIT_SIZE+1))//255,BIT_SIZE)
        rl = min((r*(BIT_SIZE+1))//255,BIT_SIZE)
        val = (((1<<bl)-1)<<16)|(((1<<gl)-1)<<8)|((1<<rl)-1)
        print('0x{:06X},'.format(val),end='')
    print('},')
print('},')
