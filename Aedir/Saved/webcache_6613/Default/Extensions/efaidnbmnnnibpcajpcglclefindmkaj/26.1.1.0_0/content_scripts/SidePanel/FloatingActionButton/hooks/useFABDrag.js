/*************************************************************************
* ADOBE CONFIDENTIAL
* ___________________
*
*  Copyright 2015 Adobe Systems Incorporated
*  All Rights Reserved.
*
* NOTICE:  All information contained herein is, and remains
* the property of Adobe Systems Incorporated and its suppliers,
* if any.  The intellectual and technical concepts contained
* herein are proprietary to Adobe Systems Incorporated and its
* suppliers and are protected by all applicable intellectual property laws,
* including trade secret and or copyright laws.
* Dissemination of this information or reproduction of this material
* is strictly forbidden unless prior written permission is obtained
* from Adobe Systems Incorporated.
**************************************************************************/
import{useState,useEffect,useCallback,useRef}from"react";import{sendAnalyticsEvent}from"../utils/fabUtils";export const useFABDrag=({containerRef:e,iframeRef:t,fabManager:a,isDraggedRef:r,onDragStart:n})=>{const[o,s]=useState(a.fabDraggedTop||null),[c,i]=useState(!1),u=useRef(0);useEffect((()=>{a.fabDraggedTop&&s(a.fabDraggedTop)}),[a.fabDraggedTop]);const g=useCallback((t=>{if(0!==t.button||t.ctrlKey)return;t.stopPropagation(),t.preventDefault(),i(!0),a.isFABActiveForDrag=!0,r.current=!1,n?.();const o=e.current;if(o){const e=o.getBoundingClientRect();u.current=t.clientY-e.top}}),[a,r,n,e]),f=useCallback((t=>{if(!c&&!a.isFABActiveForDrag)return;const n=e.current;if(!n)return;r.current=!0;let o=t.clientY-u.current;const i=n.offsetHeight,g=window.innerHeight-i-20;o=Math.max(20,Math.min(g,o)),a.fabDraggedTop=o,s(o)}),[c,a,r,e]),l=useCallback((()=>{(c||a.isFABActiveForDrag)&&(i(!1),a.isFABActiveForDrag=!1,r.current&&(window.dcLocalStorage.setItem("genAIFabTopPosition",a.fabDraggedTop),sendAnalyticsEvent([["DCBrowserExt:SidePanel:FabIcon:Dragged"]]),chrome.runtime.sendMessage({main_op:"log-info",log:{message:"FAB dragged",fabTop:`${a.fabDraggedTop}px`}})))}),[c,a,r]);useEffect((()=>(document.addEventListener("mousemove",f),document.addEventListener("mouseup",l),()=>{document.removeEventListener("mousemove",f),document.removeEventListener("mouseup",l)})),[f,l]);return{fabTop:o,handleDragHandleMouseDown:useCallback((e=>{g(e)}),[g]),handleIconMouseDown:useCallback((e=>{g(e)}),[g]),isFABActiveForDrag:c}};