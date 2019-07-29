
const { assert } = require('chai')
const { spawn } = require("child_process")
const { Process }= require('./lib/index')

describe('a process monitor', () => {

  it('can monitor the exit status of multiple processes', done => {
    const spawned1 = spawn('sleep 0.2', { shell: true })
    const spawned2 = spawn('sleep 0.3', { shell: true })
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

})

