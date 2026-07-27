/**
 * DSS: connect, optional load, dump words at fixed secondary stub addresses.
 *
 *   dss.sh dss-mem-dump.js <ccxml> [elf]
 */
importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);

var args = this.arguments;
if (args.length < 1) {
	System.err.println("usage: dss-mem-dump.js <ccxml> [elf]");
	java.lang.System.exit(2);
}

var ccxml = String(args[0]);
var elf = (args.length >= 2) ? String(args[1]) : null;
var env = ScriptingEnvironment.instance();
var server = env.getServer("DebugServer.1");
var session = null;
var rc = 1;

env.setScriptTimeout(120000);

function dump_word(addr)
{
	var v = session.memory.readWord(addr);
	/* Rhino may return Long; print as hex. */
	System.out.println("mem[0x" + Integer.toHexString(addr) + "]=0x" +
			   Long.toHexString(v >>> 0));
}

try {
	server.setConfig(ccxml);
	session = server.openSession("*", "C29xx_CPU1");
	session.target.connect();
	session.target.reset();
	if (elf != null) {
		System.out.println("load " + elf);
		session.memory.loadProgram(elf);
	}
	dump_word(0x20100000);
	dump_word(0x20110000);
	dump_word(0x20110040);
	dump_word(0x20118000);
	dump_word(0x20118040);
	dump_word(0x20117F80);
	dump_word(0x2011FF80);
	rc = 0;
} catch (ex) {
	System.err.println("dss-mem-dump FAIL " + ex);
	rc = 1;
} finally {
	try {
		if (session != null)
			session.target.disconnect();
	} catch (ignore) {
	}
	try {
		server.stop();
	} catch (ignore) {
	}
}

java.lang.System.exit(rc);
