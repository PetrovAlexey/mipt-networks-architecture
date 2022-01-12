/* eslint-env browser */

let pc = new RTCPeerConnection({
  iceServers: [{
    urls: 'stun:stun.l.google.com:19302'
  }]
})
let log = msg => {
  document.getElementById('div').innerHTML += msg + '<br>'
}

pc.ontrack = function(event) {
  var el = document.createElement(event.track.kind)
  el.srcObject = event.streams[0]
  el.autoplay = true
  el.controls = true

  document.getElementById('remoteVideos').appendChild(el)
}

pc.oniceconnectionstatechange = e => log(pc.iceConnectionState)
//pc.onicecandidate = event => {
//  if (event.candidate === null) {
//    document.getElementById('localSessionDescription').value = btoa(JSON.stringify(pc.localDescription))
//  }
//}

// Offer to receive 1 audio, and 1 video track
/*pc.addTransceiver('video', {
  'direction': 'sendrecv'
})
pc.addTransceiver('audio', {
  'direction': 'sendrecv'
})*/

//var offerDesc = new RTCSessionDescription(offer.rtc);

//pc.createOffer().then(d => pc.setLocalDescription(d)).catch(log)


window.copySDP = () => {

  let serverOffer = document.getElementById('localSessionDescription').value;
  try {
      pc.setRemoteDescription(new RTCSessionDescription(JSON.parse(serverOffer)))
  } catch (e) {
      alert(e)
  }

  pc.createAnswer().then(d => {
                                pc.setLocalDescription(d)
                                document.getElementById('remoteSessionDescription').value = JSON.stringify(d)
                                }).catch(log)


}
