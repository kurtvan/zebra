I'd like to generate some thoughts about the node editor. 


Okay, I'm just going to do a large, long train of thought here. For the node editor, screen real estate is at a premium, so we need to be sure that we are using it wisely.
There are a couple of improvements that I want to list out here:
- Navigating can be challenging, so I use a touchpad on a MacBook. Primarily, I'd like pinch-to-zoom to work, and I'd like to maintain the current scrolling behavior. Command scrolling should still work the same way.
- We should also offer buttons on each of the panes, the bottom left and right. That will minimize them so that we can maximize the amount of screen real estate that is being dedicated to the actual editing, because the pages on the bottom and sides don't necessarily matter all the time.
- We should have a button to recenter the graph on the screen. Sometimes it can get lost.
- As a maybe future improvement, some kind of mini map on the bottom of the window

Also, the nodes just aren't working right now in their current form. You have to zoom in far too much in order to edit them. We should really avoid simplifying their UI at all based on zoom view. Let's make them, for now, always visible in their fully controllable form for any node that has a zoom thumbnail preview. Let's just pause the preview or not generate new frames for it if it's small, because that's wasted cycles. 

Also, to improve the Milkdrop preset editor, let's at least add a button that selects a random one and maybe arrow buttons that cycle between the next in the list, so that we can play around more. This is going to be an overall theme around playfulness that I want to explore. 

Sorry, I do see now. There's a fit button that makes sense. 

Eventually, we'll also want to have a pane that just lists all of the nodes. This will be important for just keeping track of them and eventually doing other operations with them. 

Some more ideas:
- To create group nodes and to give some nodes a reusable kind of interface that you could use later. That, I think, will be super helpful for generating reusable ideas.
- We want audio nodes that can take the audio input and move audio through wires. There will be some video wires and some audio wires.
- We'll also want a node that essentially can process audio and can output various values about it: take the audio, split it out into the bass, treble, and mid audio. That will be important.
- The wires, I think, could use a revamp. They are somewhat pleasing right now, but I think we'll want to visually move from a kind of curve-based appearance to a more, I guess you could say, circuit board appearance. I'd like to explore having right angles, or perhaps 45° angles, and make them move in a deterministic way between nodes. I think this would be more pleasing and a little bit, maybe, easier to follow.
- I think that node organization might be a somewhat challenging thing, just spatially. This is maybe a feature to compact nodes in a way that is standardized based on their connections and in a way that minimizes the maximum of width and height. We shouldn't make a really long chain because that'll be hard to hold in space. We shouldn't make a really high chain because that will be hard to hold in space, but one that maximizes for squareness, if that makes sense, of compaction.

We will also want wires that carry regular number data. These could be things like an LFO value, which is synced to the beat clock.

Also, we'll eventually want some kind of beat detection algorithm. There, I believe, are existing open-source ones that we can use. We should leverage those so that we can convert them into data that can modulate other values.

Ultimately, we should be able to modulate basically any of the values in a device, in a node. I think that would become very powerful.

All of these things, I think, are things that need to be done. Some maybe are more urgent. I think the things that I care the most about are probably the preset switching and overall node traversal.
