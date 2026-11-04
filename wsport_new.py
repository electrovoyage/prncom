import select
import asyncio
import pyvirtualserial
from websockets.asyncio.server import serve, ServerConnection

MAX_READ = 4096

to_websocket = asyncio.Queue(128)
to_pty = asyncio.Queue(128)

port = pyvirtualserial.VirtualSerial(baudrate=115200, timeout=1)
print(f'pyvirtualserial created pty at {port.get_slave_name()}')

global connection
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
    asyncio.get_event_loop().create_task(pty_writer())
    server = await serve(wsHandler, '0.0.0.0', 7080)
    print('serving websocket')
    await server.serve_forever()
    
async def readPtyLoop():
    global connection
    print('pty loop read starting')
    while True:
        if connection is None:
            await asyncio.sleep(0.1)
            continue
        
        ready, _, _ = await asyncio.to_thread(
            select.select, [port._reader], [], [], 0.05
        )
        if not ready: continue
        
        data = await asyncio.to_thread(port.read, MAX_READ)
        if not data:
            await asyncio.sleep(0.001)
            continue
        
        try:
            await connection.send(data)
            await asyncio.sleep(0)
        except Exception:
            if connection is not None:
                try:
                    await connection.close()
                except Exception:
                    pass
                
                connection = None
                
            continue
    
print('Starting up')
eventLoop = asyncio.new_event_loop()
asyncio.set_event_loop(eventLoop)
eventLoop.create_task(readPtyLoop())
eventLoop.run_until_complete(main())
eventLoop.close()