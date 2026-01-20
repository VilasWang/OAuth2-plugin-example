const net = require('net');
const client = new net.Socket();

client.connect(6379, '127.0.0.1', function () {
    console.log('Connected to Redis');

    // 1. Send AUTH
    client.write('AUTH 123456\r\n');
});

client.on('data', function (data) {
    const response = data.toString();
    console.log('Received: ' + response);

    if (response.includes('OK')) {
        // If this was the response to AUTH (or previous command), send HMSET if not already sent
        // A simple state machine or just a timeout-based sequence is easier for this one-off script
    }
});

// Use a simple timeout sequence to avoid complex parsing logic for this test script
client.on('connect', () => {
    setTimeout(() => {
        // 2. Send HMSET after AUTH has likely processed
        console.log('Sending HMSET...');
        const cmd = 'HMSET oauth2:client:vue-client redirect_uris "[\\"http://localhost:5173/callback\\"]" secret "07d6a768c24964c6ce4cb1aa30a490d03166067c649b8c2d045061c3426c8c1b" salt "sultz"\r\n';
        client.write(cmd);
    }, 500);

    setTimeout(() => {
        console.log('Closing connection...');
        client.destroy();
    }, 1000);
});

client.on('error', function (err) {
    console.error('Error: ' + err.message);
    client.destroy();
});

client.on('close', function () {
    console.log('Connection closed');
});
