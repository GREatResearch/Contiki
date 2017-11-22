importPackage(java.io);

sim.setSpeedLimit(1.0);
var motes = sim.getMotes();

var index = Math.floor(Math.random() * motes.length);

var i = 0;
while (i < motes.length) {

    YIELD();

    if (msg.startsWith("Starting")) {
        i++;
    }
}

for (i = 0; i < motes.length; i++) {
    write(motes[i], (index == i) ? "1" : "0");
}