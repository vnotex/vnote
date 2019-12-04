GID = function(id){ return document.getElementById(id) };

function encode64_(data) {
 r = "";
 for (i=0; i<data.length; i+=3) {
  if (i+2==data.length) {
   r +=append3bytes(data[i], data[i+1], 0);
  } else if (i+1==data.length) {
   r += append3bytes(data[i], 0, 0);
  } else {
   r += append3bytes(data[i], data[i+1], data[i+2]);
  }
}
return r;
}

function append3bytes(b1, b2, b3) {
c1 = b1 >> 2;
c2 = ((b1 & 0x3) << 4) | (b2 >> 4);
c3 = ((b2 & 0xF) << 2) | (b3 >> 6);
c4 = b3 & 0x3F;
r = "";
r += encode6bit(c1 & 0x3F);
r += encode6bit(c2 & 0x3F);
r += encode6bit(c3 & 0x3F);
r += encode6bit(c4 & 0x3F);
return r;
}

function encode6bit(b) {
if (b < 10) {
 return String.fromCharCode(48 + b);
}
b -= 10;
if (b < 26) {
 return String.fromCharCode(65 + b);
}
b -= 26;
if (b < 26) {
 return String.fromCharCode(97 + b);
}
b -= 26;
if (b == 0) {
 return '-';
}
if (b == 1) {
 return '_';
}
return '?';
}