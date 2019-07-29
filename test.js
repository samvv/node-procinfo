
const { assert } = require('chai')
const { spawn } = require("child_process")
const { Process }= require('./lib/index')

function spawnSibling(cmd) {
  const spawned = spawn(cmd, { shell: true, detach: true, stdio: 'ignore' })
  spawned.unref()
  return spawned
}

describe('a process monitor', () => {

  it('can monitor the exit status of multiple processes', done => {
    const spawned1 = spawnSibling('sleep 0.2')
    const spawned2 = spawnSibling('sleep 0.3')
    const proc1 = new Process(spawned1.pid)
    const proc2 = new Process(spawned2.pid)
    assert.isOk(proc1.running)
    assert.isOk(proc2.running)
    let exitCount = 0
    proc1.on('exit', code => {
      assert.strictEqual(code, 0)
      assert.isNotOk(proc1.running)
      exitCount++
      if (exitCount == 2) done()
    })
    proc2.on('exit', code => {
      assert.strictEqual(code, 0)
      assert.isNotOk(proc2.running)
      exitCount++
      if (exitCount == 2) done()
    })
  })

  it('errors when providing an invalid PID', () => {
    assert.throws(() => new Process(0), Error);
  })

  it('can kill a process', () => {
    // Make sure to sleep longer than the test timeout
    const spawned = spawnSibling('sleep 10')
    const proc = new Process(spawned.pid)
    proc.kill()
  })

  it('can detach from a connected process', () => {
    // Make sure to sleep longer than the test timeout
    const spawned = spawnSibling('sleep 10')
    const proc = new Process(spawned.pid)
    proc.close()
  })

})

