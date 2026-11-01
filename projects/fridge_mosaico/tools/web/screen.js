// GSPF framebuffer viewer. All widgets and pointer handling execute in native C.
(() => {
  const canvas=document.getElementById('screen'), ctx=canvas.getContext('2d');
  let socket, image=ctx.createImageData(480,480), ident=0, allowed=false;
  function connect(){
    socket=new WebSocket('ws://127.0.0.1:3222/ws'); socket.binaryType='arraybuffer';
    socket.onopen=()=>socket.send(JSON.stringify({jsonrpc:'2.0',id:++ident,method:'capabilities',params:{}}));
    socket.onclose=()=>{allowed=false;setTimeout(connect,1500);};
    socket.onerror=()=>socket.close();
    socket.onmessage=e=>{
      if(typeof e.data==='string'){
        const result=JSON.parse(e.data);
        if(result.result?.browser_input_allowed!==undefined)allowed=result.result.browser_input_allowed;
        return;
      }
      const bytes=new Uint8Array(e.data), view=new DataView(e.data);
      if(bytes.length<36 || String.fromCharCode(...bytes.slice(0,4))!=='GSPF')return;
      const format=bytes[5], width=view.getUint32(16,true),height=view.getUint32(20,true),count=view.getUint32(28,true);
      if(width!==480 || height!==480 || format>1)return;
      let offset=36+count*16;
      for(let i=0;i<count;i++){
        const o=36+i*16,x1=view.getInt32(o,true),y1=view.getInt32(o+4,true),x2=view.getInt32(o+8,true),y2=view.getInt32(o+12,true);
        if(x1<0||y1<0||x2>480||y2>480)return;
        for(let y=y1;y<y2;y++)for(let x=x1;x<x2;x++){
          const p=(y*480+x)*4;
          if(format===0){const v=view.getUint16(offset,true);offset+=2;image.data[p]=((v>>11)&31)*255/31;image.data[p+1]=((v>>5)&63)*255/63;image.data[p+2]=(v&31)*255/31;}
          else{image.data[p]=bytes[offset+2];image.data[p+1]=bytes[offset+1];image.data[p+2]=bytes[offset];offset+=3;}
          image.data[p+3]=255;
        }
      }
      ctx.putImageData(image,0,0);
    };
  }
  function pointer(e,pressed){
    if(!allowed||socket.readyState!==WebSocket.OPEN)return;
    const rect=canvas.getBoundingClientRect();
    socket.send(JSON.stringify({jsonrpc:'2.0',method:'feed_pointer',params:{x:Math.max(0,Math.min(479,Math.round((e.clientX-rect.left)*480/rect.width))),y:Math.max(0,Math.min(479,Math.round((e.clientY-rect.top)*480/rect.height))),pressed}}));
  }
  canvas.onpointerdown=e=>{canvas.setPointerCapture(e.pointerId);pointer(e,true);};
  canvas.onpointermove=e=>{if(e.buttons)pointer(e,true);};
  canvas.onpointerup=e=>pointer(e,false);canvas.onpointercancel=e=>pointer(e,false);
  connect();
})();
