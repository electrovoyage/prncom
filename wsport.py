import os
import asyncio
from io import BufferedReader
from pyvirtualserial import VirtualSerial
from websockets.asyncio.server import serve, ServerConnection

global eventLoop
eventLoop = asyncio.new_event_loop()
asyncio.set_event_loop(eventLoop)

port = VirtualSerial(baudrate=115200, port=4000, timeout=1)
print(f'Serial port: {port.get_slave_name()}')

global connection
connection: ServerConnection = None

async def handler(websocket: ServerConnection):
    print('event handler running')
    global connection
    connection = websocket
    async for message in websocket:
        print(message.decode('utf8', errors='replace'), end='', flush=True)
        await asyncio.to_thread(lambda: port.write(message))
        
async def getAvailableBytes() -> int:
    if os.name == 'nt':
        return port._reader.in_waiting
    else:
        reader: BufferedReader = port._reader
        return await asyncio.to_thread(lambda: len(reader.peek(0)))

async def sendToWSLoop():
    global connection
    while True:
        if connection is None: 
            await asyncio.sleep(1)
            continue
        available = await getAvailableBytes()
        #print(available)
        if available == 0:
            await asyncio.sleep(0.5)
            continue
        data = await asyncio.to_thread(lambda: port.read(available))
        #print(f'read {len(data)}')
        if len(data) > 0:
            #print(f'tty: {data.decode('utf8', errors='replace')}')
            try:
                await connection.send(data)
                print(data.decode('utf8', errors='replace'), end='', flush=True)
            except Exception as e:
                print(e)
                continue
        else:
            await asyncio.sleep(0.1)

async def main():
    print('creating server')
    server = await serve(handler, '0.0.0.0', 7080)
    print('serving websocket')
    await server.serve_forever()

print('Starting websocket server')

eventLoop.create_task(sendToWSLoop())
eventLoop.run_until_complete(main())
eventLoop.close()