const aedes = require('aedes')();
const net = require('net');
const mysql = require('mysql');

// MQTT broker port
const PORT = 1234;

// Start TCP server for MQTT broker
const server = net.createServer(aedes.handle);

// Database connection (masih bisa digunakan untuk simpan benang dari RGB kalau mau)
const db = mysql.createConnection({
    host: 'localhost',
    user: 'root',
    password: '',
    database: 'fabricmatch'
});

db.connect(err => {
    if (err) console.error('❌ DB Connection Error:', err);
    else console.log('✅ Connected to MySQL Database');
});

server.listen(PORT, '0.0.0.0', () => {
    console.log(`🚀 MQTT Broker running on port ${PORT}`);
});

// Variabel global untuk simpan RGB terakhir
let lastScannedRGB = null;

// Handler pesan masuk (publish dari alat)
aedes.on('publish', async (packet, client) => {
    const message = packet.payload.toString();
    console.log(`📨 Message received: ${message}`);

    if (!message.startsWith('{')) return;

    try {
        const json = JSON.parse(message);

        const { jenis, r, g, b } = json;

        if (['benang', 'kain'].includes(jenis) && [r, g, b].every(v => typeof v === 'number')) {
            lastScannedRGB = { R: r, G: g, B: b };

            const topicOut = jenis === 'benang'
                ? 'fabricmatch/benang/hasil'
                : 'fabricmatch/kain/hasil';

            console.log(`🎯 Data RGB (${jenis}): R=${r}, G=${g}, B=${b}`);

            aedes.publish({
                topic: topicOut,
                payload: JSON.stringify({ R: r, G: g, B: b }),
                qos: 0,
                retain: false
            });
        }

    } catch (err) {
        console.error('❌ JSON Parse Error:', err);
    }
});
