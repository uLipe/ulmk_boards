/**
 * After a running SMP image (or load here): dump SSU state, force GEL-style
 * CPU2 release, try to read CPU2 PC.
 *
 *   dss.sh dss-smp-probe.js <ccxml> [elf]
 *
 * If elf is given: ram_init + loadProgram + run 2s + halt, then probe.
 * If omitted: attach to already-running CPU1.
 */
importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);

var args = this.arguments;
if (args.length < 1) {
	System.err.println("usage: dss-smp-probe.js <ccxml> [elf]");
	java.lang.System.exit(2);
}

var ccxml = String(args[0]);
var elf = (args.length >= 2) ? String(args[1]) : null;
var env = ScriptingEnvironment.instance();
var server = env.getServer("DebugServer.1");
var cpu1 = null;
var cpu2 = null;
var rc = 1;

env.setScriptTimeout(120000);

function hex32(v)
{
	var u = (v >>> 0);
	var s = ("00000000" + u.toString(16)).slice(-8);
	return "0x" + s;
}

function rd32(session, addr)
{
	return session.memory.readWord(0, addr) >>> 0;
}

function wr32(session, addr, val)
{
	session.memory.writeWord(0, addr, val >>> 0);
}

function dump(session, tag)
{
	System.out.println("---- " + tag + " ----");
	System.out.println("RSTSTAT=" + hex32(rd32(session, 0x301803B0)));
	System.out.println("LSEN=" + hex32(rd32(session, 0x30180348)));
	System.out.println("LINK2_AP=" + hex32(rd32(session, 0x3008000C)));
	System.out.println("CPU2 VECT/LINK/CTRL=" +
		hex32(rd32(session, 0x30082000)) + " " +
		hex32(rd32(session, 0x30082004)) + " " +
		hex32(rd32(session, 0x30082008)));
	System.out.println("CPU3 VECT/CTRL=" +
		hex32(rd32(session, 0x30083000)) + " " +
		hex32(rd32(session, 0x30083008)));
	System.out.println("LPA1[0]=" + hex32(rd32(session, 0x20108000)));
	System.out.println("CPA0[0]=" + hex32(rd32(session, 0x20110000)));
	System.out.println("magic2/3=" +
		hex32(rd32(session, 0x200F8000)) + " " +
		hex32(rd32(session, 0x200F8004)));
}

try {
	server.setConfig(ccxml);
	cpu1 = server.openSession("*", "C29xx_CPU1");
	System.out.println("probe: connect CPU1");
	cpu1.target.connect();

	if (elf != null) {
		cpu1.target.reset();
		try {
			cpu1.expression.evaluate("ram_init()");
			System.out.println("probe: ram_init OK");
		} catch (e) {
			System.out.println("probe: ram_init skip " + e);
		}
		/*
		 * Disable auto-run-to-main: our ELF has no main halt label and
		 * loadProgram otherwise times out for minutes.
		 */
		try {
			cpu1.options.setBoolean(
				"AutoRunToLabelOnRestart", false);
		} catch (e) {
			System.out.println("probe: AutoRun opt skip " + e);
		}
		System.out.println("probe: load " + elf);
		try {
			cpu1.memory.loadProgram(elf);
		} catch (e) {
			System.out.println("probe: load warn " + e);
		}
		try {
			cpu1.target.runAsynch();
		} catch (e) {
			System.out.println("probe: runAsynch warn " + e);
		}
		java.lang.Thread.sleep(2500);
		try {
			cpu1.target.halt();
		} catch (e) {
			System.out.println("probe: halt warn " + e);
		}
	} else {
		try {
			cpu1.target.halt();
		} catch (e) {
			System.out.println("probe: halt warn " + e);
		}
	}

	dump(cpu1, "after app");

	/* Plant GEL IDLE at 0x20107FE0 and release CPU2 the GEL way. */
	System.out.println("probe: GEL-style CPU2 release");
	wr32(cpu1, 0x20107FE0, 0x0c319002);
	wr32(cpu1, 0x20107FE4, 0x0b52117f);
	wr32(cpu1, 0x20107FE8, 0x0b52117f);
	wr32(cpu1, 0x20107FEC, 0x00003510);
	wr32(cpu1, 0x30180348, 0);
	wr32(cpu1, 0x3008000C, 0xffffffff);
	wr32(cpu1, 0x30082000, 0x20107FE0);
	wr32(cpu1, 0x30082004, 2);
	wr32(cpu1, 0x30082010, 0x20107FE0);
	wr32(cpu1, 0x30082014, 2);
	wr32(cpu1, 0x30082008, 0x36);
	java.lang.Thread.sleep(300);
	dump(cpu1, "after GEL CPU2 release");

	try {
		cpu2 = server.openSession("*", "C29xx_CPU2");
		System.out.println("probe: connect CPU2");
		cpu2.target.connect();
		java.lang.Thread.sleep(100);
		try {
			cpu2.target.halt();
		} catch (e) {
			System.out.println("CPU2 halt: " + e);
		}
		var names = ["PC", "RPC", "pc", "PCXI"];
		for (var i = 0; i < names.length; i++) {
			try {
				var v = cpu2.memory.readRegister(names[i]);
				System.out.println("CPU2 reg " + names[i] +
						   "=" + hex32(v));
			} catch (e) {
				System.out.println("CPU2 reg " + names[i] +
						   " fail");
			}
		}
		try {
			var regs = cpu2.memory.getRegisterDescriptions();
			var n = regs.length;
			System.out.println("CPU2 nregs=" + n);
			var lim = (n < 40) ? n : 40;
			for (var j = 0; j < lim; j++) {
				System.out.println("  reg[" + j + "]=" +
						   regs[j].getName());
			}
		} catch (e) {
			System.out.println("CPU2 reg list fail " + e);
		}
	} catch (e) {
		System.out.println("CPU2 session fail " + e);
	}

	rc = 0;
} catch (ex) {
	System.err.println("probe FAIL " + ex);
	rc = 1;
} finally {
	try { if (cpu2 != null) cpu2.target.disconnect(); } catch (i) {}
	try { if (cpu1 != null) cpu1.target.disconnect(); } catch (i) {}
	try { server.stop(); } catch (i) {}
}

java.lang.System.exit(rc);
