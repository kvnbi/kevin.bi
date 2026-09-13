importScripts('blitz.js');

const queue = [];
let send = null;

onmessage = (event) => {
  if (send) send(event.data);
  else queue.push(event.data);
};

createBlitz({ print: (line) => postMessage(line) }).then((engine) => {
  engine._blitz_init();
  send = engine.cwrap('blitz_command', null, ['string']);
  postMessage('ready');
  for (const command of queue) send(command);
  queue.length = 0;
});
