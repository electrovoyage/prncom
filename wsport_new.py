import os
import errno
import uringcore
import socket
import asyncio
import pyvirtualserial
from picows.websockets.asyncio.server import serve, ServerConnection

asyncio.set_event_loop_policy(uringcore.EventLoopPolicy())

MAX_READ = 4096

to_websocket = asyncio.Queue(128)
to_pty = asyncio.Queue(128)

port = pyvirtualserial.VirtualSerial(baudrate=115200, timeout=0)
print(f'pyvirtualserial created pty at {port.get_slave_name()}')

global connection, eventLoop
connection: ServerConnection = None

async def pty_writer():
    while True:
        data = await to_pty.get()
        
        try:
            await asyncio.wait_for(
                asyncio.to_thread(port.write, data),
                timeout=2
            )
        except asyncio.TimeoutError:
            print('pty write timed out, closing connection', flush=True)
            if connection is not None:
                await connection.close()
                
        finally:
            to_pty.task_done()
                
async def wsHandler(websocket: ServerConnection):
    global connection
    
    sock = websocket.transport.get_extra_info('socket')
    if sock is not None:
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    
    connection = websocket
    print('handler started')
    
    try:
        async for message in websocket:
            try:
                to_pty.put_nowait(message)
            except asyncio.QueueFull:
                print('write queue full, closing connection', flush=True)
                await websocket.close()

                break;
    finally:
        if connection is websocket:
            connection = None
            
async def main():
    global eventLoop
    eventLoop.create_task(pty_writer())
    eventLoop.create_task(readPtyLoop())
    server = await serve(wsHandler, '0.0.0.0', 7080)
    print('serving websocket')
    await server.serve_forever()
    
async def readPtyLoop():
    global connection, eventLoop
    
    fd = port._reader.fileno()
    
    def on_readable():
        try:
            data = os.read(fd, MAX_READ)
        except BlockingIOError:
            return
        except OSError as e:
            if e.errno in (errno.EIO, errno.EBADF):
                print('pty closed', flush=True)
                eventLoop.remove_reader(fd)
                return
            
            raise
        
        if not data:
            eventLoop.remove_reader(fd)
            return
        
        try:
            to_websocket.put_nowait(data)
        except asyncio.QueueFull:
            eventLoop.remove_reader(fd)
            
    eventLoop.add_reader(fd, on_readable)
    
    print('pty loop read starting')
    try:
        while True:
            data = await to_websocket.get()
            
            eventLoop.add_reader(fd, on_readable)
            
            if connection is None:
                continue
            
            try:
                await connection.send(data)
            except Exception as e:
                print(f'ws send failed: {e}', flush=True)
                if connection is not None:
                    try:
                        await connection.close()
                    except Exception:
                        pass
                    connection = None
    finally:
        eventLoop.remove_reader(fd)
    
print('Starting up')
eventLoop = asyncio.new_event_loop()
asyncio.set_event_loop(eventLoop)
#eventLoop.create_task(readPtyLoop())
eventLoop.run_until_complete(main())
eventLoop.close()