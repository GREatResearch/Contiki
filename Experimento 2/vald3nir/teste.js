importPackage(java.io);

sim.setSpeedLimit(1.0);
var motes = sim.getMotes();

i = 0;
while(i < motes.length){	
	
	YIELD();	
	
	if(msg.startsWith("Starting")){

		console.log("entrou");
		i++;
	}
}