import { spawn } from "node:child_process";
import { createServer } from "node:net";

const serverExecutable = process.argv[2];
if (!serverExecutable) {
	throw new Error("missing server executable");
}

const expectedSizes = [511, 512, 513, 4096];

function makeJsonMessage(nBytes) {
	const prefix = '{"payload":"';
	const suffix = '"}';
	return prefix + "x".repeat(nBytes - prefix.length - suffix.length) + suffix;
}

const expected = expectedSizes.map(makeJsonMessage);

const probe = createServer();
await new Promise((resolve, reject) => {
	probe.once("error", reject);
	probe.listen(0, "127.0.0.1", resolve);
});
const address = probe.address();
const port = address.port;
await new Promise((resolve) => probe.close(resolve));

const child = spawn(serverExecutable, [String(port)], {
	stdio: ["ignore", "pipe", "pipe"],
});

const childExit = new Promise((resolve, reject) => {
	child.once("error", reject);
	child.once("exit", (code, signal) => resolve({ code, signal }));
});

let childOutput = "";
child.stdout.on("data", (chunk) => {
	childOutput += chunk.toString();
});
child.stderr.on("data", (chunk) => {
	childOutput += chunk.toString();
});

const received = [[], []];
const clients = [];

function connectClient(i) {
	return new Promise((resolve, reject) => {
		const ws = new WebSocket(`ws://127.0.0.1:${port}`);
		clients.push(ws);

		ws.addEventListener("open", resolve, { once: true });
		ws.addEventListener("error", () => reject(new Error(`client ${i} failed to connect`)), {
			once: true,
		});
		ws.addEventListener("message", (event) => {
			received[i].push(event.data);
		});
	});
}

try {
	const connectDeadline = Date.now() + 5000;
	while (true) {
		try {
			await Promise.all([connectClient(0), connectClient(1)]);
			break;
		} catch {
			for (const ws of clients.splice(0))
				ws.close();
			if (Date.now() >= connectDeadline)
				throw new Error("clients could not connect before deadline");
			await new Promise((resolve) => setTimeout(resolve, 25));
		}
	}

	for (const message of expected)
		clients[0].send(message + "EOJ");

	const receiveDeadline = Date.now() + 5000;
	while (
		(received[0].length < expected.length || received[1].length < expected.length) &&
		Date.now() < receiveDeadline
	) {
		await new Promise((resolve) => setTimeout(resolve, 10));
	}

	for (let i = 0; i < received.length; ++i) {
		if (received[i].length !== expected.length) {
			throw new Error(
				`client ${i} received ${received[i].length} frames, expected ${expected.length}`,
			);
		}

		for (let j = 0; j < expected.length; ++j) {
			if (received[i][j] !== expected[j]) {
				throw new Error(
					`client ${i} frame ${j} was ${Buffer.byteLength(received[i][j])} bytes; ` +
						`expected one intact ${expectedSizes[j]}-byte frame`,
				);
			}
			JSON.parse(received[i][j]);
		}
	}

	const result = await Promise.race([
		childExit,
		new Promise((_, reject) => {
			const timer = setTimeout(
				() => reject(new Error("server did not exit normally")), 5000,
			);
			timer.unref();
		}),
	]);
	if (result.code !== 0 || result.signal !== null) {
		throw new Error(
			`server exited with code ${result.code} and signal ${result.signal}`,
		);
	}
} catch (error) {
	throw new Error(`${error.message}\nserver output:\n${childOutput}`);
} finally {
	for (const ws of clients)
		ws.close();
	if (child.exitCode === null)
		child.kill("SIGTERM");
}
