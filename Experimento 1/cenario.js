importPackage(java.io);

sim.setSpeedLimit(1.0);
var motes = sim.getMotes();

i = 0;
while (i < motes.length) {
    YIELD();
    if (msg.startsWith("Starting")) {
        i++;
    }
}

write(motes[0], "DT" + motes.length + "Q0C0R");
write(motes[1], "mistT" + motes.length + "Q0C3R");
write(motes[2], "mistT" + motes.length + "Q0C5R");
write(motes[3], "fogT" + motes.length + "Q0C15R");
write(motes[4], "fogT" + motes.length + "Q0C20R");
write(motes[5], "cloudT" + motes.length + "Q5C200R");
write(motes[6], "cloudT" + motes.length + "Q10C250R");

//for(i=1; i<motes.length; i++) write(motes[i], "mistT" + motes.length + "Q0C0R");